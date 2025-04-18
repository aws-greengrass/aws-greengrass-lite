// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_authz.h"
#include "../../ipc_server.h"
#include "../../ipc_service.h"
#include "../../ipc_subscriptions.h"
#include "mqttproxy.h"
#include <ggl/arena.h>
#include <ggl/base64.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/aws_iot_mqtt.h>
#include <ggl/error.h>
#include <ggl/ipc/common.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

static GglError subscribe_to_iot_core_callback(
    GglObject data, uint32_t resp_handle, int32_t stream_id, GglArena *alloc
) {
    GglBuffer topic;
    GglBuffer payload;

    GglError ret
        = ggl_aws_iot_mqtt_subscribe_parse_resp(data, &topic, &payload);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GglBuffer base64_payload;
    ret = ggl_base64_encode(payload, alloc, &base64_payload);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Insufficent memory to base64 encode payload; skipping.");
        return GGL_ERR_OK;
    }

    GglObject response = ggl_obj_map(
        GGL_MAP({ GGL_STR("message"),
                  ggl_obj_map(GGL_MAP(
                      { GGL_STR("topicName"), ggl_obj_buf(topic) },
                      { GGL_STR("payload"), ggl_obj_buf(base64_payload) }
                  )) })
    );

    ret = ggl_ipc_response_send(
        resp_handle,
        stream_id,
        GGL_STR("aws.greengrass#IoTCoreMessage"),
        response
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "Failed to send subscription response with error %s; skipping.",
            ggl_strerror(ret)
        );
    }

    return GGL_ERR_OK;
}

GglError ggl_handle_subscribe_to_iot_core(
    const GglIpcOperationInfo *info,
    GglMap args,
    uint32_t handle,
    int32_t stream_id,
    GglIpcError *ipc_error,
    GglArena *alloc
) {
    (void) alloc;

    GglObject *topic_name_obj;
    GglObject *qos_obj;
    GglError ret = ggl_map_validate(
        args,
        GGL_MAP_SCHEMA(
            { GGL_STR("topicName"), true, GGL_TYPE_BUF, &topic_name_obj },
            { GGL_STR("qos"), false, GGL_TYPE_BUF, &qos_obj },
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

    int64_t qos = 0;
    if (qos_obj != NULL) {
        ret = ggl_str_to_int64(ggl_obj_into_buf(*qos_obj), &qos);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to parse 'qos' string value.");
            *ipc_error = (GglIpcError
            ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
                .message = GGL_STR("Failed to parse 'qos' string value.") };
            return ret;
        }
        if ((qos < 0) || (qos > 2)) {
            GGL_LOGE("'qos' not a valid value.");
            *ipc_error = (GglIpcError
            ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
                .message = GGL_STR("'qos' not a valid value.") };
            return GGL_ERR_INVALID;
        }
    }

    ret = ggl_ipc_auth(info, topic_name, ggl_ipc_mqtt_policy_matcher);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("IPC Operation not authorized.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_UNAUTHORIZED_ERROR,
            .message = GGL_STR("IPC Operation not authorized.") };
        return GGL_ERR_INVALID;
    }

    GglMap call_args = GGL_MAP(
        { GGL_STR("topic_filter"), *topic_name_obj },
        { GGL_STR("qos"), ggl_obj_i64(qos) },
    );

    ret = ggl_ipc_bind_subscription(
        handle,
        stream_id,
        GGL_STR("aws_iot_mqtt"),
        GGL_STR("subscribe"),
        call_args,
        subscribe_to_iot_core_callback,
        NULL
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to bind the subscription.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GGL_STR("Failed to bind the subscription.") };
        return ret;
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GGL_STR("aws.greengrass#SubscribeToIoTCoreResponse"),
        ggl_obj_map((GglMap) { 0 })
    );
}
