/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ggl/buffer.h"
#include "ggl/log.h"
#include "ggl/map.h"
#include "ggl/object.h"
#include "ggl/server.h"
#include "mqtt.h"
#include <errno.h>
#include <stdint.h>

static void rpc_publish(GglMap params, GglResponseHandle *handle) {
    IotcoredMsg msg = { 0 };
    uint8_t qos = 0;

    GglObject *val;

    if (ggl_map_get(params, GGL_STR("topic"), &val)
        && (val->type == GGL_TYPE_BUF)) {
        GglBuffer topic = val->buf;
        if (topic.len > UINT16_MAX) {
            GGL_LOGE("rpc-handler", "Publish payload too large.");
            ggl_respond(handle, EOVERFLOW, GGL_OBJ_NULL());
            return;
        }
        msg.topic = topic;
    } else {
        GGL_LOGE("rpc-handler", "Publish received invalid arguments.");
        ggl_respond(handle, EINVAL, GGL_OBJ_NULL());
        return;
    }

    if (ggl_map_get(params, GGL_STR("payload"), &val)) {
        if (val->type != GGL_TYPE_BUF) {
            GGL_LOGE("rpc-handler", "Publish received invalid arguments.");
            ggl_respond(handle, EINVAL, GGL_OBJ_NULL());
            return;
        }
        msg.payload = val->buf;
    }

    if (ggl_map_get(params, GGL_STR("qos"), &val)) {
        if ((val->type != GGL_TYPE_I64) || (val->i64 < 0) || (val->i64 > 2)) {
            GGL_LOGE("rpc-handler", "Publish received invalid arguments.");
            ggl_respond(handle, EINVAL, GGL_OBJ_NULL());
            return;
        }
        qos = (uint8_t) val->i64;
    }

    int ret = iotcored_mqtt_publish(&msg, qos);

    if (ret != 0) {
        ggl_respond(handle, EIO, GGL_OBJ_NULL());
    } else {
        ggl_respond(handle, 0, GGL_OBJ_NULL());
    }
}

void ggl_receive_callback(
    void *ctx, GglBuffer method, GglList params, GglResponseHandle *handle
) {
    (void) ctx;

    if ((params.len < 1) || (params.items[0].type != GGL_TYPE_MAP)) {
        GGL_LOGE("rpc-handler", "Received invalid arguments.");
        ggl_respond(handle, EINVAL, GGL_OBJ_NULL());
        return;
    }

    GglMap param_map = params.items[0].map;

    if (ggl_buffer_eq(method, GGL_STR("publish"))) {
        rpc_publish(param_map, handle);
    } else {
        GGL_LOGE(
            "rpc-handler",
            "Received unknown command: %.*s.",
            (int) method.len,
            method.data
        );
        ggl_respond(handle, EINVAL, GGL_OBJ_NULL());
    }
}
