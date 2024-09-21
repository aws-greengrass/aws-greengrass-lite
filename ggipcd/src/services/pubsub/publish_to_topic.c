// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_server.h"
#include "pubsub.h"
#include <ggl/alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

GglError ggl_handle_publish_to_topic(
    GglMap args, uint32_t handle, int32_t stream_id, GglAlloc *alloc
) {
    (void) alloc;

    GglObject *val = NULL;
    bool found = ggl_map_get(args, GGL_STR("topic"), &val);
    if (!found) {
        GGL_LOGE("PublishToTopic", "Missing topic.");
        return GGL_ERR_INVALID;
    }
    if (val->type != GGL_TYPE_BUF) {
        GGL_LOGE("PublishToTopic", "topic not a string.");
        return GGL_ERR_INVALID;
    }
    GglBuffer topic = val->buf;

    found = ggl_map_get(args, GGL_STR("publishMessage"), &val);
    if (!found) {
        GGL_LOGE("PublishToTopic", "Missing publishMessage.");
        return GGL_ERR_INVALID;
    }
    if (val->type != GGL_TYPE_MAP) {
        GGL_LOGE("PublishToTopic", "publishMessage not a map.");
        return GGL_ERR_INVALID;
    }
    GglMap publish_message = val->map;

    bool is_json;

    found = ggl_map_get(publish_message, GGL_STR("jsonMessage"), &val);
    if (found) {
        found = ggl_map_get(publish_message, GGL_STR("binaryMessage"), NULL);
        if (found) {
            GGL_LOGE(
                "PublishToTopic",
                "publishMessage has both binaryMessage and jsonMessage."
            );
            return GGL_ERR_INVALID;
        }

        is_json = true;
    } else {
        found = ggl_map_get(publish_message, GGL_STR("binaryMessage"), &val);
        if (!found) {
            GGL_LOGE(
                "PublishToTopic",
                "publishMessage missing binaryMessage or jsonMessage."
            );
            return GGL_ERR_INVALID;
        }

        is_json = false;
    }

    if (val->type != GGL_TYPE_MAP) {
        GGL_LOGE(
            "PublishToTopic",
            "%sMessage not a map.",
            is_json ? "json" : "binary"
        );
        return GGL_ERR_INVALID;
    }

    found = ggl_map_get(val->map, GGL_STR("message"), &val);
    if (!found) {
        GGL_LOGE(
            "PublishToTopic",
            "Missing message in %sMessage.",
            is_json ? "json" : "binary"
        );
        return GGL_ERR_INVALID;
    }
    if (val->type != GGL_TYPE_BUF) {
        GGL_LOGE("PublishToTopic", "message is not a string.");
        return GGL_ERR_INVALID;
    }

    GglBuffer message = val->buf;

    GglMap call_args = GGL_MAP(
        { GGL_STR("topic"), GGL_OBJ(topic) },
        { GGL_STR("type"),
          is_json ? GGL_OBJ_STR("json") : GGL_OBJ_STR("base64") },
        { GGL_STR("message"), GGL_OBJ(message) },
    );

    GglError ret = ggl_call(
        GGL_STR("pubsub"), GGL_STR("publish"), call_args, NULL, NULL, NULL
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GGL_STR("aws.greengrass#PublishToTopicResponse"),
        GGL_OBJ_MAP()
    );
}
