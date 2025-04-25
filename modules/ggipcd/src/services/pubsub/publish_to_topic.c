// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_authz.h"
#include "../../ipc_server.h"
#include "../../ipc_service.h"
#include "pubsub.h"
#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/flags.h>
#include <ggl/ipc/error.h>
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
    GglIpcError *ipc_error,
    GglArena *alloc
) {
    (void) alloc;

    GglObject *topic_obj;
    GglObject *publish_message_obj;
    GglError ret = ggl_map_validate(
        args,
        GGL_MAP_SCHEMA(
            { GGL_STR("topic"), GGL_REQUIRED, GGL_TYPE_BUF, &topic_obj },
            { GGL_STR("publishMessage"),
              GGL_REQUIRED,
              GGL_TYPE_MAP,
              &publish_message_obj },
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
    GglMap publish_message = ggl_obj_into_map(*publish_message_obj);

    GglObject *json_message;
    GglObject *binary_message;
    ret = ggl_map_validate(
        publish_message,
        GGL_MAP_SCHEMA(
            { GGL_STR("jsonMessage"),
              GGL_OPTIONAL,
              GGL_TYPE_MAP,
              &json_message },
            { GGL_STR("binaryMessage"),
              GGL_OPTIONAL,
              GGL_TYPE_MAP,
              &binary_message },
        )
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Received invalid parameters.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GGL_STR("Received invalid parameters.") };
        return GGL_ERR_INVALID;
    }

    if ((json_message == NULL) == (binary_message == NULL)) {
        GGL_LOGE("'publishMessage' must have exactly one of 'binaryMessage' or "
                 "'jsonMessage'.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GGL_STR("Received invalid parameters.") };
        return GGL_ERR_INVALID;
    }

    bool is_json = json_message != NULL;

    GglObject *message;
    ret = ggl_map_validate(
        ggl_obj_into_map(*(is_json ? json_message : binary_message)),
        GGL_MAP_SCHEMA(
            { GGL_STR("message"),
              GGL_REQUIRED,
              is_json ? GGL_TYPE_NULL : GGL_TYPE_BUF,
              &message },
        )
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Received invalid parameters.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GGL_STR("Received invalid parameters.") };
        return GGL_ERR_INVALID;
    }

    ret = ggl_ipc_auth(info, topic, ggl_ipc_default_policy_matcher);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("IPC Operation not authorized.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_UNAUTHORIZED_ERROR,
            .message = GGL_STR("IPC Operation not authorized.") };
        return GGL_ERR_INVALID;
    }

    GglMap call_args = GGL_MAP(
        { GGL_STR("topic"), *topic_obj },
        { GGL_STR("type"),
          is_json ? ggl_obj_buf(GGL_STR("json"))
                  : ggl_obj_buf(GGL_STR("base64")) },
        { GGL_STR("message"), *message },
    );

    ret = ggl_call(
        GGL_STR("gg_pubsub"), GGL_STR("publish"), call_args, NULL, NULL, NULL
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to publish the message.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GGL_STR("Failed to publish the message.") };
        return ret;
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GGL_STR("aws.greengrass#PublishToTopicResponse"),
        ggl_obj_map((GglMap) { 0 })
    );
}
