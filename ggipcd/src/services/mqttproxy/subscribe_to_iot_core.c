// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_authz.h"
#include "../../ipc_server.h"
#include "../../ipc_service.h"
#include "../../ipc_subscriptions.h"
#include "mqttproxy.h"
#include <ggl/alloc.h>
#include <ggl/base64.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/aws_iot_mqtt.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

static GglError subscribe_to_iot_core_callback(
    GglObject data, uint32_t resp_handle, int32_t stream_id, GglAlloc *alloc
) {
    GglBuffer *topic;
    GglBuffer *payload;

    GglError ret
        = ggl_aws_iot_mqtt_subscribe_parse_resp(data, &topic, &payload);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GglBuffer base64_payload;
    ret = ggl_base64_encode(*payload, alloc, &base64_payload);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "SubscribeToIoTCore",
            "Insufficent memory to base64 encode payload; skipping."
        );
        return GGL_ERR_OK;
    }

    GglObject response
        = GGL_OBJ_MAP({ GGL_STR("message"),
                        GGL_OBJ_MAP(
                            { GGL_STR("topicName"), GGL_OBJ(*topic) },
                            { GGL_STR("payload"), GGL_OBJ(base64_payload) }
                        ) });

    ret = ggl_ipc_response_send(
        resp_handle,
        stream_id,
        GGL_STR("aws.greengrass#IoTCoreMessage"),
        response
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "SubscribeToIoTCore",
            "Failed to send subscription response; skipping."
        );
        return GGL_ERR_OK;
    }

    return GGL_ERR_OK;
}

GglError ggl_handle_subscribe_to_iot_core(
    const GglIpcOperationInfo *info,
    GglMap args,
    uint32_t handle,
    int32_t stream_id,
    GglAlloc *alloc
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
        GGL_LOGE("SubscribeToIoTCore", "Received invalid parameters.");
        return GGL_ERR_INVALID;
    }

    int64_t qos = 0;
    if (qos_obj != NULL) {
        ret = ggl_str_to_int64(qos_obj->buf, &qos);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("SubscribeToIoTCore", "Failed to parse qos string value.");
            return ret;
        }
        if ((qos < 0) || (qos > 2)) {
            GGL_LOGE("SubscribeToIoTCore", "qos not a valid value.");
            return GGL_ERR_INVALID;
        }
    }

    ret = ggl_ipc_auth(info, topic_name_obj->buf, ggl_ipc_mqtt_policy_matcher);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("SubscribeToIotCore", "IPC Operation not authorized.");
        return GGL_ERR_INVALID;
    }

    GglMap call_args = GGL_MAP(
        { GGL_STR("topic_filter"), *topic_name_obj },
        { GGL_STR("qos"), GGL_OBJ_I64(qos) },
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
        return ret;
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GGL_STR("aws.greengrass#SubscribeToIoTCoreResponse"),
        GGL_OBJ_MAP()
    );
}
