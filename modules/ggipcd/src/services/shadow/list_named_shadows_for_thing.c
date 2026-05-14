// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_error.h"
#include "../../ipc_server.h"
#include "../../ipc_service.h"
#include "shadow.h"
#include <gg/arena.h>
#include <gg/base64.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/flags.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <ggl/core_bus/aws_iot_mqtt.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    uint32_t handle;
    int32_t stream_id;
    bool response_received;
    GgArena alloc;
} ListNamedShadowsContext;

static GgError on_shadow_response(void *ctx, uint32_t handle, GgObject data) {
    (void) handle;
    ListNamedShadowsContext *shadow_ctx = ctx;

    GgBuffer topic;
    GgBuffer payload;
    GgError ret = ggl_aws_iot_mqtt_subscribe_parse_resp(data, &topic, &payload);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to parse shadow response.");
        return GG_ERR_OK;
    }

    if (shadow_ctx->response_received) {
        return GG_ERR_OK;
    }
    shadow_ctx->response_received = true;

    GgBuffer b64_payload;
    ret = gg_base64_encode(payload, &shadow_ctx->alloc, &b64_payload);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to encode shadow payload.");
        (void) ggl_ipc_response_send(
            shadow_ctx->handle,
            shadow_ctx->stream_id,
            GG_STR("aws.greengrass#ServiceError"),
            (GgMap) { 0 }
        );
        return GG_ERR_OK;
    }

    (void) ggl_ipc_response_send(
        shadow_ctx->handle,
        shadow_ctx->stream_id,
        GG_STR("aws.greengrass#ListNamedShadowsForThingResponse"),
        GG_MAP(gg_kv(GG_STR("results"), gg_obj_buf(b64_payload)))
    );
    return GG_ERR_OK;
}

static void on_shadow_close(void *ctx, uint32_t handle) {
    (void) handle;
    ListNamedShadowsContext *shadow_ctx = ctx;

    if (!shadow_ctx->response_received) {
        (void) ggl_ipc_response_send(
            shadow_ctx->handle,
            shadow_ctx->stream_id,
            GG_STR("aws.greengrass#ServiceError"),
            (GgMap) { 0 }
        );
    }
}

GgError ggl_handle_list_named_shadows_for_thing(
    const GglIpcOperationInfo *info,
    GgMap args,
    uint32_t handle,
    int32_t stream_id,
    GglIpcError *ipc_error,
    GgArena *alloc
) {
    (void) info;
    GgObject *thing_name_obj;
    GgObject *page_size_obj;
    GgObject *next_token_obj;
    GgError ret = gg_map_validate(
        args,
        GG_MAP_SCHEMA(
            { GG_STR("thingName"), GG_REQUIRED, GG_TYPE_BUF, &thing_name_obj },
            { GG_STR("pageSize"), GG_OPTIONAL, GG_TYPE_I64, &page_size_obj },
            { GG_STR("nextToken"), GG_OPTIONAL, GG_TYPE_BUF, &next_token_obj },
        )
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Received invalid parameters.");
        *ipc_error = (GglIpcError) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
                                     .message
                                     = GG_STR("Received invalid parameters.") };
        return GG_ERR_INVALID;
    }

    GgBuffer thing_name = gg_obj_into_buf(*thing_name_obj);

    static uint8_t topic_mem[256];
    int topic_len = snprintf(
        (char *) topic_mem,
        sizeof(topic_mem),
        "$aws/things/%.*s/shadow/get",
        (int) thing_name.len,
        thing_name.data
    );
    if (topic_len < 0 || (size_t) topic_len >= sizeof(topic_mem)) {
        GG_LOGE("Failed to format shadow topic.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GG_STR("Failed to format shadow topic.") };
        return GG_ERR_NOMEM;
    }
    GgBuffer topic = { .data = topic_mem, .len = (size_t) topic_len };

    static uint8_t filter_mem[256];
    int filter_len = snprintf(
        (char *) filter_mem,
        sizeof(filter_mem),
        "$aws/things/%.*s/shadow/get/+",
        (int) thing_name.len,
        thing_name.data
    );
    if (filter_len < 0 || (size_t) filter_len >= sizeof(filter_mem)) {
        GG_LOGE("Failed to format shadow filter.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GG_STR("Failed to format shadow filter.") };
        return GG_ERR_NOMEM;
    }
    GgBuffer filter = { .data = filter_mem, .len = (size_t) filter_len };

    ListNamedShadowsContext *ctx = gg_arena_alloc(
        alloc, sizeof(ListNamedShadowsContext), alignof(ListNamedShadowsContext)
    );
    if (ctx == NULL) {
        *ipc_error
            = (GglIpcError) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
                              .message = GG_STR("Memory allocation failed.") };
        return GG_ERR_NOMEM;
    }
    *ctx = (ListNamedShadowsContext) {
        .handle = handle,
        .stream_id = stream_id,
        .response_received = false,
        .alloc = *alloc,
    };

    uint32_t sub_handle;
    ret = ggl_aws_iot_mqtt_subscribe(
        GG_STR("aws_iot_mqtt"),
        (GgBufList) { .bufs = &filter, .len = 1 },
        0,
        true,
        on_shadow_response,
        on_shadow_close,
        ctx,
        &sub_handle
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to subscribe to shadow response topic.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GG_STR("Failed to subscribe to shadow response.") };
        return ret;
    }

    ret = ggl_aws_iot_mqtt_publish(
        GG_STR("aws_iot_mqtt"), topic, GG_STR(""), 0, false
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to publish shadow list request.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GG_STR("Failed to publish shadow request.") };
        return ret;
    }

    return GG_ERR_OK;
}
