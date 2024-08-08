// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../ipc_server.h"
#include "handlers.h"
#include <ggl/alloc.h>
#include <ggl/base64.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

GglError handle_publish_to_iot_core(
    GglMap args, uint32_t handle, int32_t stream_id, GglAlloc *alloc
) {
    GglBuffer topic;
    GglBuffer payload;
    int64_t qos;

    GglObject *val = NULL;
    bool found = ggl_map_get(args, GGL_STR("topicName"), &val);
    if (!found) {
        GGL_LOGE("PublishToIoTCore", "Missing topicName.");
        return GGL_ERR_INVALID;
    }
    if (val->type != GGL_TYPE_BUF) {
        GGL_LOGE("PublishToIoTCore", "topicName not a string.");
        return GGL_ERR_INVALID;
    }
    topic = val->buf;

    found = ggl_map_get(args, GGL_STR("payload"), &val);
    if (!found) {
        payload = GGL_STR("");
    } else {
        if (val->type != GGL_TYPE_BUF) {
            GGL_LOGE("PublishToIoTCore", "topicName not a string.");
            return GGL_ERR_INVALID;
        }
        payload = val->buf;
    }

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
                    "PublishToIoTCore", "Failed to parse qos string value."
                );
                return ret;
            }
        } else {
            GGL_LOGE("PublishToIoTCore", "qos not an valid type.");
            return GGL_ERR_INVALID;
        }
    }

    bool decoded = ggl_base64_decode_in_place(&payload);
    if (!decoded) {
        GGL_LOGE("PublishToIoTCore", "payload is not valid base64.");
        return GGL_ERR_INVALID;
    }

    GglMap call_args = GGL_MAP(
        { GGL_STR("topic"), GGL_OBJ(topic) },
        { GGL_STR("payload"), GGL_OBJ(payload) },
        { GGL_STR("qos"), GGL_OBJ_I64(qos) },
    );

    GglObject call_resp;
    GglError ret = ggl_call(
        GGL_STR("/aws/ggl/iotcored"),
        GGL_STR("publish"),
        call_args,
        NULL,
        alloc,
        &call_resp
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
