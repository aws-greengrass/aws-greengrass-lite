// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_server.h"
#include "../../ipc_service.h"
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

GglError ggl_handle_publish_to_iot_core(
    const GglIpcOperationInfo *info,
    GglMap args,
    uint32_t handle,
    int32_t stream_id,
    GglAlloc *alloc
) {
    (void) alloc;
    (void) info;

    GglObject *topic_name_obj;
    GglObject *payload_obj;
    GglObject *qos_obj;
    GglError ret = ggl_map_validate(
        args,
        GGL_MAP_SCHEMA(
            { GGL_STR("topicName"), true, GGL_TYPE_BUF, &topic_name_obj },
            { GGL_STR("payload"), false, GGL_TYPE_BUF, &payload_obj },
            { GGL_STR("qos"), false, GGL_TYPE_BUF, &qos_obj },
        )
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("PublishToIotCore", "Received invalid paramters.");
        return GGL_ERR_INVALID;
    }

    GglBuffer payload = GGL_STR("");
    if (payload_obj != NULL) {
        payload = payload_obj->buf;
    }

    int64_t qos = 0;
    if (qos_obj != NULL) {
        ret = ggl_str_to_int64(qos_obj->buf, &qos);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("PublishToIoTCore", "Failed to parse qos string value.");
            return ret;
        }
        if ((qos < 0) || (qos > 2)) {
            GGL_LOGE("PublishToIoTCore", "qos not a valid value.");
            return GGL_ERR_INVALID;
        }
    }

    bool decoded = ggl_base64_decode_in_place(&payload);
    if (!decoded) {
        GGL_LOGE("PublishToIoTCore", "payload is not valid base64.");
        return GGL_ERR_INVALID;
    }

    ret = ggl_aws_iot_mqtt_publish(
        topic_name_obj->buf, payload, (uint8_t) qos, true
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GGL_STR("aws.greengrass#PublishToIoTCoreResponse"),
        GGL_OBJ_MAP()
    );
}
