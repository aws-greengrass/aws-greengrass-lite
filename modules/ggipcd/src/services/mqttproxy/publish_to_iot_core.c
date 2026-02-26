// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_authz.h"
#include "../../ipc_error.h"
#include "../../ipc_server.h"
#include "../../ipc_service.h"
#include "mqttproxy.h"
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
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

GgError ggl_handle_publish_to_iot_core(
    const GglIpcOperationInfo *info,
    GgMap args,
    uint32_t handle,
    int32_t stream_id,
    GglIpcError *ipc_error,
    GgArena *alloc
) {
    (void) alloc;

    GgObject *topic_name_obj;
    GgObject *payload_obj;
    GgObject *qos_obj;
    GgError ret = gg_map_validate(
        args,
        GG_MAP_SCHEMA(
            { GG_STR("topicName"), GG_REQUIRED, GG_TYPE_BUF, &topic_name_obj },
            { GG_STR("payload"), GG_OPTIONAL, GG_TYPE_BUF, &payload_obj },
            { GG_STR("qos"), GG_OPTIONAL, GG_TYPE_NULL, &qos_obj },
        )
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Received invalid parameters.");
        *ipc_error = (GglIpcError) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
                                     .message
                                     = GG_STR("Received invalid parameters.") };
        return GG_ERR_INVALID;
    }

    GgBuffer topic_name = gg_obj_into_buf(*topic_name_obj);

    GG_LOGT(
        "topic_name_obj buffer: %.*s with length: %zu",
        (int) topic_name.len,
        topic_name.data,
        topic_name.len
    );

    GgBuffer payload = GG_STR("");
    if (payload_obj != NULL) {
        payload = gg_obj_into_buf(*payload_obj);
    }

    int64_t qos = 0;
    if (qos_obj != NULL) {
        switch (gg_obj_type(*qos_obj)) {
        case GG_TYPE_BUF:
            ret = gg_str_to_int64(gg_obj_into_buf(*qos_obj), &qos);
            if (ret != GG_ERR_OK) {
                GG_LOGE("Failed to parse 'qos' string value.");
                *ipc_error = (GglIpcError
                ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
                    .message = GG_STR("Failed to parse 'qos' string value.") };
                return ret;
            }
            break;
        case GG_TYPE_I64:
            qos = gg_obj_into_i64(*qos_obj);
            break;
        default:
            GG_LOGE("Key qos of invalid type.");
            *ipc_error = (GglIpcError
            ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
                .message = GG_STR("Key qos of invalid type.") };
            return GG_ERR_INVALID;
        }
        if ((qos < 0) || (qos > 2)) {
            GG_LOGE("'qos' not a valid value.");
            *ipc_error = (GglIpcError
            ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
                .message = GG_STR("'qos' not a valid value.") };
            return GG_ERR_INVALID;
        }
    }

    bool decoded = gg_base64_decode_in_place(&payload);
    if (!decoded) {
        GG_LOGE("'payload' is not valid base64.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GG_STR("'payload' is not valid base64.") };
        return GG_ERR_INVALID;
    }

    ret = ggl_ipc_auth(info, topic_name, ggl_ipc_mqtt_policy_matcher);
    if (ret != GG_ERR_OK) {
        GG_LOGE("IPC Operation not authorized.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_UNAUTHORIZED_ERROR,
            .message = GG_STR("IPC Operation not authorized.") };
        return GG_ERR_INVALID;
    }

    ret = ggl_aws_iot_mqtt_publish(
        GG_STR("aws_iot_mqtt"), topic_name, payload, (uint8_t) qos, true
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
        GG_STR("aws.greengrass#PublishToIoTCoreResponse"),
        (GgMap) { 0 }
    );
}
