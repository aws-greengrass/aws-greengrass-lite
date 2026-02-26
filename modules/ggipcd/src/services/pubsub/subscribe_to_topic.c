// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_authz.h"
#include "../../ipc_error.h"
#include "../../ipc_server.h"
#include "../../ipc_service.h"
#include "../../ipc_subscriptions.h"
#include "pubsub.h"
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/flags.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

static GgError subscribe_to_topic_callback(
    GgObject data, uint32_t resp_handle, int32_t stream_id, GgArena *alloc
) {
    (void) alloc;

    if (gg_obj_type(data) != GG_TYPE_MAP) {
        GG_LOGE("Subscription response not a map.");
        return GG_ERR_FAILURE;
    }

    GgObject *topic_obj;
    GgObject *type_obj;
    GgObject *message_obj;
    GgError ret = gg_map_validate(
        gg_obj_into_map(data),
        GG_MAP_SCHEMA(
            { GG_STR("topic"), GG_REQUIRED, GG_TYPE_BUF, &topic_obj },
            { GG_STR("type"), GG_REQUIRED, GG_TYPE_BUF, &type_obj },
            { GG_STR("message"), GG_REQUIRED, GG_TYPE_NULL, &message_obj },
        )
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Received invalid subscription response.");
        return ret;
    }
    GgBuffer type = gg_obj_into_buf(*type_obj);

    bool is_json;

    if (gg_buffer_eq(type, GG_STR("json"))) {
        is_json = true;
    } else if (gg_buffer_eq(type, GG_STR("base64"))) {
        is_json = false;
        if (gg_obj_type(*message_obj) != GG_TYPE_BUF) {
            GG_LOGE("Received invalid message type.");
            return GG_ERR_INVALID;
        }
    } else {
        GG_LOGE(
            "Received unknown subscription response type: %.*s.",
            (int) type.len,
            type.data
        );
        return GG_ERR_INVALID;
    }

    GgObject inner = gg_obj_map(GG_MAP(
        gg_kv(GG_STR("message"), *message_obj),
        gg_kv(
            GG_STR("context"),
            gg_obj_map(GG_MAP(gg_kv(GG_STR("topic"), *topic_obj)))
        )
    ));

    GgMap response = GG_MAP(
        gg_kv(is_json ? GG_STR("jsonMessage") : GG_STR("binaryMessage"), inner)
    );

    ret = ggl_ipc_response_send(
        resp_handle,
        stream_id,
        GG_STR("aws.greengrass#SubscriptionResponseMessage"),
        response
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to send subscription response; skipping.");
        return GG_ERR_OK;
    }

    return GG_ERR_OK;
}

GgError ggl_handle_subscribe_to_topic(
    const GglIpcOperationInfo *info,
    GgMap args,
    uint32_t handle,
    int32_t stream_id,
    GglIpcError *ipc_error,
    GgArena *alloc
) {
    (void) alloc;

    GgObject *topic_obj;
    GgError ret = gg_map_validate(
        args,
        GG_MAP_SCHEMA(
            { GG_STR("topic"), GG_REQUIRED, GG_TYPE_BUF, &topic_obj },
        )
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Received invalid parameters.");
        *ipc_error = (GglIpcError) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
                                     .message
                                     = GG_STR("Received invalid parameters.") };
        return GG_ERR_INVALID;
    }
    GgBuffer topic = gg_obj_into_buf(*topic_obj);

    ret = ggl_ipc_auth(info, topic, ggl_ipc_default_policy_matcher);
    if (ret != GG_ERR_OK) {
        GG_LOGE("IPC Operation not authorized.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_UNAUTHORIZED_ERROR,
            .message = GG_STR("IPC Operation not authorized.") };
        return GG_ERR_INVALID;
    }

    GgMap call_args = GG_MAP(gg_kv(GG_STR("topic_filter"), *topic_obj));

    ret = ggl_ipc_bind_subscription(
        handle,
        stream_id,
        GG_STR("gg_pubsub"),
        GG_STR("subscribe"),
        call_args,
        subscribe_to_topic_callback,
        NULL
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to bind subscription.");
        *ipc_error = (GglIpcError) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
                                     .message
                                     = GG_STR("Failed to bind subscription.") };
        return ret;
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GG_STR("aws.greengrass#SubscribeToTopicResponse"),
        (GgMap) { 0 }
    );
}
