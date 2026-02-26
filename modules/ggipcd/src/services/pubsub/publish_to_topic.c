// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_authz.h"
#include "../../ipc_error.h"
#include "../../ipc_server.h"
#include "../../ipc_service.h"
#include "pubsub.h"
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/flags.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <ggl/core_bus/client.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

GgError ggl_handle_publish_to_topic(
    const GglIpcOperationInfo *info,
    GgMap args,
    uint32_t handle,
    int32_t stream_id,
    GglIpcError *ipc_error,
    GgArena *alloc
) {
    (void) alloc;

    GgObject *topic_obj;
    GgObject *publish_message_obj;
    GgError ret = gg_map_validate(
        args,
        GG_MAP_SCHEMA(
            { GG_STR("topic"), GG_REQUIRED, GG_TYPE_BUF, &topic_obj },
            { GG_STR("publishMessage"),
              GG_REQUIRED,
              GG_TYPE_MAP,
              &publish_message_obj },
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
    GgMap publish_message = gg_obj_into_map(*publish_message_obj);

    GgObject *json_message;
    GgObject *binary_message;
    ret = gg_map_validate(
        publish_message,
        GG_MAP_SCHEMA(
            { GG_STR("jsonMessage"), GG_OPTIONAL, GG_TYPE_MAP, &json_message },
            { GG_STR("binaryMessage"),
              GG_OPTIONAL,
              GG_TYPE_MAP,
              &binary_message },
        )
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Received invalid parameters.");
        *ipc_error = (GglIpcError) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
                                     .message
                                     = GG_STR("Received invalid parameters.") };
        return GG_ERR_INVALID;
    }

    if ((json_message == NULL) == (binary_message == NULL)) {
        GG_LOGE(
            "'publishMessage' must have exactly one of 'binaryMessage' or 'jsonMessage'."
        );
        *ipc_error = (GglIpcError) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
                                     .message
                                     = GG_STR("Received invalid parameters.") };
        return GG_ERR_INVALID;
    }

    bool is_json = json_message != NULL;

    GgObject *message;
    ret = gg_map_validate(
        gg_obj_into_map(*(is_json ? json_message : binary_message)),
        GG_MAP_SCHEMA(
            { GG_STR("message"),
              GG_REQUIRED,
              is_json ? GG_TYPE_NULL : GG_TYPE_BUF,
              &message },
        )
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Received invalid parameters.");
        *ipc_error = (GglIpcError) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
                                     .message
                                     = GG_STR("Received invalid parameters.") };
        return GG_ERR_INVALID;
    }

    ret = ggl_ipc_auth(info, topic, ggl_ipc_default_policy_matcher);
    if (ret != GG_ERR_OK) {
        GG_LOGE("IPC Operation not authorized.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_UNAUTHORIZED_ERROR,
            .message = GG_STR("IPC Operation not authorized.") };
        return GG_ERR_INVALID;
    }

    GgMap call_args = GG_MAP(
        gg_kv(GG_STR("topic"), *topic_obj),
        gg_kv(
            GG_STR("type"),
            is_json ? gg_obj_buf(GG_STR("json")) : gg_obj_buf(GG_STR("base64"))
        ),
        gg_kv(GG_STR("message"), *message),
    );

    ret = ggl_call(
        GG_STR("gg_pubsub"), GG_STR("publish"), call_args, NULL, NULL, NULL
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to publish the message.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GG_STR("Failed to publish the message.") };
        return ret;
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GG_STR("aws.greengrass#PublishToTopicResponse"),
        (GgMap) { 0 }
    );
}
