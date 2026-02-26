// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_authz.h"
#include "../../ipc_error.h"
#include "../../ipc_server.h"
#include "../../ipc_service.h"
#include "../../ipc_subscriptions.h"
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
#include <stdint.h>
#include <stdlib.h>

static GgError subscribe_to_iot_core_callback(
    GgObject data, uint32_t resp_handle, int32_t stream_id, GgArena *alloc
) {
    GgBuffer topic;
    GgBuffer payload;

    GgError ret = ggl_aws_iot_mqtt_subscribe_parse_resp(data, &topic, &payload);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    GgBuffer base64_payload;
    ret = gg_base64_encode(payload, alloc, &base64_payload);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Insufficent memory to base64 encode payload; skipping.");
        return GG_ERR_OK;
    }

    GgMap response = GG_MAP(gg_kv(
        GG_STR("message"),
        gg_obj_map(GG_MAP(
            gg_kv(GG_STR("topicName"), gg_obj_buf(topic)),
            gg_kv(GG_STR("payload"), gg_obj_buf(base64_payload))
        ))
    ));

    ret = ggl_ipc_response_send(
        resp_handle,
        stream_id,
        GG_STR("aws.greengrass#IoTCoreMessage"),
        response
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE(
            "Failed to send subscription response with error %s; skipping.",
            gg_strerror(ret)
        );
    }

    return GG_ERR_OK;
}

GgError ggl_handle_subscribe_to_iot_core(
    const GglIpcOperationInfo *info,
    GgMap args,
    uint32_t handle,
    int32_t stream_id,
    GglIpcError *ipc_error,
    GgArena *alloc
) {
    (void) alloc;

    GgObject *topic_name_obj;
    GgObject *qos_obj;
    GgError ret = gg_map_validate(
        args,
        GG_MAP_SCHEMA(
            { GG_STR("topicName"), GG_REQUIRED, GG_TYPE_BUF, &topic_name_obj },
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
        case GG_TYPE_NULL:
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

    ret = ggl_ipc_auth(info, topic_name, ggl_ipc_mqtt_policy_matcher);
    if (ret != GG_ERR_OK) {
        GG_LOGE("IPC Operation not authorized.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_UNAUTHORIZED_ERROR,
            .message = GG_STR("IPC Operation not authorized.") };
        return GG_ERR_INVALID;
    }

    GgMap call_args = GG_MAP(
        gg_kv(GG_STR("topic_filter"), *topic_name_obj),
        gg_kv(GG_STR("qos"), gg_obj_i64(qos)),
    );

    ret = ggl_ipc_bind_subscription(
        handle,
        stream_id,
        GG_STR("aws_iot_mqtt"),
        GG_STR("subscribe"),
        call_args,
        subscribe_to_iot_core_callback,
        NULL
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to bind the subscription.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GG_STR("Failed to bind the subscription.") };
        return ret;
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GG_STR("aws.greengrass#SubscribeToIoTCoreResponse"),
        (GgMap) { 0 }
    );
}
