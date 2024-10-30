// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_authz.h"
#include "../../ipc_server.h"
#include "../../ipc_service.h"
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
    const GglIpcOperationInfo *info,
    GglMap args,
    uint32_t handle,
    int32_t stream_id,
    GglAlloc *alloc
) {
    (void) alloc;

    GglObject *topic;
    GglObject *publish_message;
    GglError ret = ggl_map_validate(
        args,
        GGL_MAP_SCHEMA(
            { GGL_STR("topic"), true, GGL_TYPE_BUF, &topic },
            { GGL_STR("publishMessage"), true, GGL_TYPE_MAP, &publish_message },
        )
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Received invalid paramters.");
        return GGL_ERR_INVALID;
    }

    GglObject *json_message;
    GglObject *binary_message;
    ret = ggl_map_validate(
        publish_message->map,
        GGL_MAP_SCHEMA(
            { GGL_STR("jsonMessage"), false, GGL_TYPE_MAP, &json_message },
            { GGL_STR("binaryMessage"), false, GGL_TYPE_MAP, &binary_message },
        )
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Received invalid paramters.");
        return GGL_ERR_INVALID;
    }

    if ((json_message == NULL) == (binary_message == NULL)) {
        GGL_LOGE("publishMessage must have exactly one of binaryMessage or "
                 "jsonMessage.");
        return GGL_ERR_INVALID;
    }

    bool is_json = json_message != NULL;

    GglObject *message;
    ret = ggl_map_validate(
        (is_json ? json_message : binary_message)->map,
        GGL_MAP_SCHEMA({ GGL_STR("message"), true, GGL_TYPE_BUF, &message }, )
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Received invalid paramters.");
        return GGL_ERR_INVALID;
    }

    ret = ggl_ipc_auth(info, topic->buf, ggl_ipc_default_policy_matcher);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("IPC Operation not authorized.");
        return GGL_ERR_INVALID;
    }

    GglMap call_args = GGL_MAP(
        { GGL_STR("topic"), *topic },
        { GGL_STR("type"),
          is_json ? GGL_OBJ_BUF(GGL_STR("json"))
                  : GGL_OBJ_BUF(GGL_STR("base64")) },
        { GGL_STR("message"), *message },
    );

    ret = ggl_call(
        GGL_STR("pubsub"), GGL_STR("publish"), call_args, NULL, NULL, NULL
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GGL_STR("aws.greengrass#PublishToTopicResponse"),
        GGL_OBJ_MAP({ 0 })
    );
}
