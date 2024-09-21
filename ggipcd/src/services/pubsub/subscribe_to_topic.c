// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_server.h"
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
        GGL_LOGE("SubscribeToTopic", "Subscription response not a map.");
        return GGL_ERR_FAILURE;
    }

    GglObject *val = NULL;
    bool found = ggl_map_get(data.map, GGL_STR("topic"), &val);
    if (!found) {
        GGL_LOGE("SubscribeToTopic", "Subscription response missing topic.");
        return GGL_ERR_FAILURE;
    }
    if (val->type != GGL_TYPE_BUF) {
        GGL_LOGE(
            "SubscribeToTopic", "Subscription response topic not a buffer."
        );
        return GGL_ERR_INVALID;
    }
    GglBuffer topic = val->buf;

    found = ggl_map_get(data.map, GGL_STR("type"), &val);
    if (!found) {
        GGL_LOGE("SubscribeToTopic", "Subscription response missing type.");
        return GGL_ERR_FAILURE;
    }
    if (val->type != GGL_TYPE_BUF) {
        GGL_LOGE(
            "SubscribeToTopic", "Subscription response type not a buffer."
        );
        return GGL_ERR_INVALID;
    }
    GglBuffer type = val->buf;

    bool is_json;

    if (ggl_buffer_eq(type, GGL_STR("json"))) {
        is_json = true;
    } else if (ggl_buffer_eq(type, GGL_STR("base64"))) {
        is_json = false;
    } else {
        GGL_LOGE(
            "SubscribeToTopic",
            "Received unknown subscription response type: %.*s.",
            (int) type.len,
            type.data
        );
        return GGL_ERR_INVALID;
    }

    found = ggl_map_get(data.map, GGL_STR("message"), &val);
    if (!found) {
        GGL_LOGE("SubscribeToTopic", "Subscription response missing message.");
        return GGL_ERR_FAILURE;
    }
    if (val->type != GGL_TYPE_BUF) {
        GGL_LOGE(
            "SubscribeToTopic", "Subscription response message not a buffer."
        );
        return GGL_ERR_INVALID;
    }
    GglBuffer message = val->buf;

    GglObject inner = GGL_OBJ_MAP(
        { GGL_STR("message"), GGL_OBJ(message) },
        { GGL_STR("context"),
          GGL_OBJ_MAP({ GGL_STR("topic"), GGL_OBJ(topic) }) }
    );

    GglObject response = GGL_OBJ_MAP(
        { is_json ? GGL_STR("jsonMessage") : GGL_STR("binaryMessage"), inner }
    );

    GglError ret = ggl_ipc_response_send(
        resp_handle,
        stream_id,
        GGL_STR("aws.greengrass#SubscriptionResponseMessage"),
        response
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "SubscribeToTopic",
            "Failed to send subscription response; skipping."
        );
        return GGL_ERR_OK;
    }

    return GGL_ERR_OK;
}

GglError ggl_handle_subscribe_to_topic(
    GglMap args, uint32_t handle, int32_t stream_id, GglAlloc *alloc
) {
    (void) alloc;

    GglObject *val = NULL;
    bool found = ggl_map_get(args, GGL_STR("topic"), &val);
    if (!found) {
        GGL_LOGE("SubscribeToTopic", "Missing topic.");
        return GGL_ERR_INVALID;
    }
    if (val->type != GGL_TYPE_BUF) {
        GGL_LOGE("SubscribeToTopic", "topic not a string.");
        return GGL_ERR_INVALID;
    }
    GglBuffer topic_filter = val->buf;

    GglMap call_args
        = GGL_MAP({ GGL_STR("topic_filter"), GGL_OBJ(topic_filter) });

    GglError ret = ggl_ipc_bind_subscription(
        handle,
        stream_id,
        GGL_STR("pubsub"),
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
        GGL_OBJ_MAP()
    );
}
