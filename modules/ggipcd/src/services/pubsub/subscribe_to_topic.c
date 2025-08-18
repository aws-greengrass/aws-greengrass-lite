// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_authz.h"
#include "../../ipc_server.h"
#include "../../ipc_service.h"
#include "../../ipc_subscriptions.h"
#include "pubsub.h"
#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/flags.h>
#include <ggl/ipc/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

static GglError subscribe_to_topic_callback(
    GglObject data, uint32_t resp_handle, int32_t stream_id, GglArena *alloc
) {
    (void) alloc;

    if (ggl_obj_type(data) != GGL_TYPE_MAP) {
        GGL_LOGE("Subscription response not a map.");
        return GGL_ERR_FAILURE;
    }

    GglObject *topic_obj;
    GglObject *type_obj;
    GglObject *message_obj;
    GglError ret = ggl_map_validate(
        ggl_obj_into_map(data),
        GGL_MAP_SCHEMA(
            { GGL_STR("topic"), GGL_REQUIRED, GGL_TYPE_BUF, &topic_obj },
            { GGL_STR("type"), GGL_REQUIRED, GGL_TYPE_BUF, &type_obj },
            { GGL_STR("message"), GGL_REQUIRED, GGL_TYPE_NULL, &message_obj },
        )
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Received invalid subscription response.");
        return ret;
    }
    GglBuffer type = ggl_obj_into_buf(*type_obj);

    bool is_json;

    if (ggl_buffer_eq(type, GGL_STR("json"))) {
        is_json = true;
    } else if (ggl_buffer_eq(type, GGL_STR("base64"))) {
        is_json = false;
        if (ggl_obj_type(*message_obj) != GGL_TYPE_BUF) {
            GGL_LOGE("Received invalid message type.");
            return GGL_ERR_INVALID;
        }
    } else {
        GGL_LOGE(
            "Received unknown subscription response type: %.*s.",
            (int) type.len,
            type.data
        );
        return GGL_ERR_INVALID;
    }

    GglObject inner = ggl_obj_map(GGL_MAP(
        ggl_kv(GGL_STR("message"), *message_obj),
        ggl_kv(
            GGL_STR("context"),
            ggl_obj_map(GGL_MAP(ggl_kv(GGL_STR("topic"), *topic_obj)))
        )
    ));

    GglMap response = GGL_MAP(ggl_kv(
        is_json ? GGL_STR("jsonMessage") : GGL_STR("binaryMessage"), inner
    ));

    ret = ggl_ipc_response_send(
        resp_handle,
        stream_id,
        GGL_STR("aws.greengrass#SubscriptionResponseMessage"),
        response
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to send subscription response; skipping.");
        return GGL_ERR_OK;
    }

    return GGL_ERR_OK;
}

GglError ggl_handle_subscribe_to_topic(
    const GglIpcOperationInfo *info,
    GglMap args,
    uint32_t handle,
    int32_t stream_id,
    GglIpcError *ipc_error,
    GglArena *alloc
) {
    (void) alloc;

    GglObject *topic_obj;
    GglError ret = ggl_map_validate(
        args,
        GGL_MAP_SCHEMA(
            { GGL_STR("topic"), GGL_REQUIRED, GGL_TYPE_BUF, &topic_obj },
        )
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Received invalid parameters.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GGL_STR("Received invalid parameters.") };
        return GGL_ERR_INVALID;
    }
    GglBuffer topic = ggl_obj_into_buf(*topic_obj);

    ret = ggl_ipc_auth(info, topic, ggl_ipc_default_policy_matcher);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("IPC Operation not authorized.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_UNAUTHORIZED_ERROR,
            .message = GGL_STR("IPC Operation not authorized.") };
        return GGL_ERR_INVALID;
    }

    GglMap call_args = GGL_MAP(ggl_kv(GGL_STR("topic_filter"), *topic_obj));

    ret = ggl_ipc_bind_subscription(
        handle,
        stream_id,
        GGL_STR("gg_pubsub"),
        GGL_STR("subscribe"),
        call_args,
        subscribe_to_topic_callback,
        NULL
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to bind subscription.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GGL_STR("Failed to bind subscription.") };
        return ret;
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GGL_STR("aws.greengrass#SubscribeToTopicResponse"),
        (GglMap) { 0 }
    );
}
