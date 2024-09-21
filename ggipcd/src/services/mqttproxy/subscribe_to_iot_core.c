// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

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
    (void) info;

    GglBuffer topic_filter;
    int64_t qos;

    GglObject *val = NULL;
    bool found = ggl_map_get(args, GGL_STR("topicName"), &val);
    if (!found) {
        GGL_LOGE("SubscribeToIoTCore", "Missing topicName.");
        return GGL_ERR_INVALID;
    }
    if (val->type != GGL_TYPE_BUF) {
        GGL_LOGE("SubscribeToIoTCore", "topicName not a string.");
        return GGL_ERR_INVALID;
    }
    topic_filter = val->buf;

    found = ggl_map_get(args, GGL_STR("qos"), &val);
    if (!found) {
        qos = 0;
    } else {
        if (val->type == GGL_TYPE_I64) {
            qos = val->i64;
        } else if (val->type == GGL_TYPE_BUF) {
            GglError ret = ggl_str_to_int64(val->buf, &qos);
            if (ret != GGL_ERR_OK) {
                GGL_LOGE(
                    "SubscribeToIoTCore", "Failed to parse qos string value."
                );
                return ret;
            }
        } else {
            GGL_LOGE("SubscribeToIoTCore", "qos not an valid type.");
            return GGL_ERR_INVALID;
        }
    }

    GglMap call_args = GGL_MAP(
        { GGL_STR("topic_filter"), GGL_OBJ(topic_filter) },
        { GGL_STR("qos"), GGL_OBJ_I64(qos) },
    );

    GglError ret = ggl_ipc_bind_subscription(
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
