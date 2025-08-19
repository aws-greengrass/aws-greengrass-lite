// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_authz.h"
#include "../../ipc_error.h"
#include "../../ipc_server.h"
#include "../../ipc_service.h"
#include "mqttproxy.h"
#include <ggl/arena.h>
#include <ggl/base64.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/aws_iot_mqtt.h>
#include <ggl/error.h>
#include <ggl/flags.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

GglError ggl_handle_publish_to_iot_core(
    const GglIpcOperationInfo *info,
    GglMap args,
    uint32_t handle,
    int32_t stream_id,
    GglIpcError *ipc_error,
    GglArena *alloc
) {
    (void) alloc;

    GglObject *topic_name_obj;
    GglObject *payload_obj;
    GglObject *qos_obj;
    GglError ret = ggl_map_validate(
        args,
        GGL_MAP_SCHEMA(
            { GGL_STR("topicName"),
              GGL_REQUIRED,
              GGL_TYPE_BUF,
              &topic_name_obj },
            { GGL_STR("payload"), GGL_OPTIONAL, GGL_TYPE_BUF, &payload_obj },
            { GGL_STR("qos"), GGL_OPTIONAL, GGL_TYPE_NULL, &qos_obj },
        )
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Received invalid parameters.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GGL_STR("Received invalid parameters.") };
        return GGL_ERR_INVALID;
    }

    GglBuffer topic_name = ggl_obj_into_buf(*topic_name_obj);

    GGL_LOGT(
        "topic_name_obj buffer: %.*s with length: %zu",
        (int) topic_name.len,
        topic_name.data,
        topic_name.len
    );

    GglBuffer payload = GGL_STR("");
    if (payload_obj != NULL) {
        payload = ggl_obj_into_buf(*payload_obj);
    }

    int64_t qos = 0;
    if (qos_obj != NULL) {
        switch (ggl_obj_type(*qos_obj)) {
        case GGL_TYPE_BUF:
            ret = ggl_str_to_int64(ggl_obj_into_buf(*qos_obj), &qos);
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to parse 'qos' string value.");
                *ipc_error = (GglIpcError
                ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
                    .message = GGL_STR("Failed to parse 'qos' string value.") };
                return ret;
            }
            break;
        case GGL_TYPE_I64:
            qos = ggl_obj_into_i64(*qos_obj);
            break;
        default:
            GGL_LOGE("Key qos of invalid type.");
            *ipc_error = (GglIpcError
            ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
                .message = GGL_STR("Key qos of invalid type.") };
            return GGL_ERR_INVALID;
        }
        if ((qos < 0) || (qos > 2)) {
            GGL_LOGE("'qos' not a valid value.");
            *ipc_error = (GglIpcError
            ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
                .message = GGL_STR("'qos' not a valid value.") };
            return GGL_ERR_INVALID;
        }
    }

    bool decoded = ggl_base64_decode_in_place(&payload);
    if (!decoded) {
        GGL_LOGE("'payload' is not valid base64.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GGL_STR("'payload' is not valid base64.") };
        return GGL_ERR_INVALID;
    }

    ret = ggl_ipc_auth(info, topic_name, ggl_ipc_mqtt_policy_matcher);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("IPC Operation not authorized.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_UNAUTHORIZED_ERROR,
            .message = GGL_STR("IPC Operation not authorized.") };
        return GGL_ERR_INVALID;
    }

    ret = ggl_aws_iot_mqtt_publish(topic_name, payload, (uint8_t) qos, true);
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
        GGL_STR("aws.greengrass#PublishToIoTCoreResponse"),
        (GglMap) { 0 }
    );
}
