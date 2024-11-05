// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_authz.h"
#include "../../ipc_server.h"
#include "../../ipc_service.h"
#include "../../ipc_subscriptions.h"
#include "pubsub.h"
#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

static GglError subscribe_to_topic_callback(
    GglObject data, uint32_t resp_handle, int32_t stream_id, GglAlloc *alloc
) {
    (void) alloc;

    if (data.type != GGL_TYPE_MAP) {
        GGL_LOGE("Subscription response not a map.");
        return GGL_ERR_FAILURE;
    }

    GglObject *topic_obj;
    GglObject *type_obj;
    GglObject *message_obj;
    GglError ret = ggl_map_validate(
        data.map,
        GGL_MAP_SCHEMA(
            { GGL_STR("topic"), true, GGL_TYPE_BUF, &topic_obj },
            { GGL_STR("type"), true, GGL_TYPE_BUF, &type_obj },
            { GGL_STR("message"), true, GGL_TYPE_BUF, &message_obj },
        )
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Received invalid subscription response.");
        return ret;
    }
    GglBuffer type = type_obj->buf;

    bool is_json;

    if (ggl_buffer_eq(type, GGL_STR("json"))) {
        is_json = true;
    } else if (ggl_buffer_eq(type, GGL_STR("base64"))) {
        is_json = false;
    } else {
        GGL_LOGE(
            "Received unknown subscription response type: %.*s.",
            (int) type.len,
            type.data
        );
        return GGL_ERR_INVALID;
    }

    GglObject inner = GGL_OBJ_MAP(GGL_MAP(
        { GGL_STR("message"), *message_obj },
        { GGL_STR("context"),
          GGL_OBJ_MAP(GGL_MAP({ GGL_STR("topic"), *topic_obj })) }
    ));

    GglObject response = GGL_OBJ_MAP(GGL_MAP(
        { is_json ? GGL_STR("jsonMessage") : GGL_STR("binaryMessage"), inner }
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
    GglAlloc *alloc
) {
    (void) alloc;

    GglObject *topic_obj;
    GglError ret = ggl_map_validate(
        args,
        GGL_MAP_SCHEMA({ GGL_STR("topic"), true, GGL_TYPE_BUF, &topic_obj }, )
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Received invalid parameters.");
        return GGL_ERR_INVALID;
    }

    ret = ggl_ipc_auth(info, topic_obj->buf, ggl_ipc_default_policy_matcher);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("IPC Operation not authorized.");
        return GGL_ERR_INVALID;
    }

    GglMap call_args = GGL_MAP({ GGL_STR("topic_filter"), *topic_obj });

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
        return ret;
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GGL_STR("aws.greengrass#SubscribeToTopicResponse"),
        GGL_OBJ_MAP({ 0 })
    );
}
