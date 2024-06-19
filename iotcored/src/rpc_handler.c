/* gravel - Utilities for AWS IoT Core clients
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "gravel/buffer.h"
#include "gravel/log.h"
#include "gravel/map.h"
#include "gravel/object.h"
#include "gravel/server.h"
#include "mqtt.h"
#include <errno.h>
#include <stdint.h>

static void rpc_publish(GravelMap params, GravelResponseHandle *handle) {
    IotcoredMsg msg = { 0 };
    uint8_t qos = 0;

    GravelObject *val;

    if (gravel_map_get(params, GRAVEL_STR("topic"), &val)
        && (val->type == GRAVEL_TYPE_BUF)) {
        GravelBuffer topic = val->buf;
        if (topic.len > UINT16_MAX) {
            GRAVEL_LOGE("rpc-handler", "Publish payload too large.");
            gravel_respond(handle, EOVERFLOW, GRAVEL_OBJ_NULL());
            return;
        }
        msg.topic = topic;
    } else {
        GRAVEL_LOGE("rpc-handler", "Publish received invalid arguments.");
        gravel_respond(handle, EINVAL, GRAVEL_OBJ_NULL());
        return;
    }

    if (gravel_map_get(params, GRAVEL_STR("payload"), &val)) {
        if (val->type != GRAVEL_TYPE_BUF) {
            GRAVEL_LOGE("rpc-handler", "Publish received invalid arguments.");
            gravel_respond(handle, EINVAL, GRAVEL_OBJ_NULL());
            return;
        }
        msg.payload = val->buf;
    }

    if (gravel_map_get(params, GRAVEL_STR("qos"), &val)) {
        if ((val->type != GRAVEL_TYPE_U64) || (val->u64 > 2)) {
            GRAVEL_LOGE("rpc-handler", "Publish received invalid arguments.");
            gravel_respond(handle, EINVAL, GRAVEL_OBJ_NULL());
            return;
        }
        qos = (uint8_t) val->u64;
    }

    int ret = iotcored_mqtt_publish(&msg, qos);

    if (ret != 0) {
        gravel_respond(handle, EIO, GRAVEL_OBJ_NULL());
    } else {
        gravel_respond(handle, 0, GRAVEL_OBJ_NULL());
    }
}

void gravel_receive_callback(
    void *ctx,
    GravelBuffer method,
    GravelList params,
    GravelResponseHandle *handle
) {
    (void) ctx;

    if ((params.len < 1) || (params.items[0].type != GRAVEL_TYPE_MAP)) {
        GRAVEL_LOGE("rpc-handler", "Received invalid arguments.");
        gravel_respond(handle, EINVAL, GRAVEL_OBJ_NULL());
        return;
    }

    GravelMap param_map = params.items[0].map;

    if (gravel_buffer_eq(method, GRAVEL_STR("publish"))) {
        rpc_publish(param_map, handle);
    } else {
        GRAVEL_LOGE(
            "rpc-handler",
            "Received unknown command: %.*s.",
            (int) method.len,
            method.data
        );
        gravel_respond(handle, EINVAL, GRAVEL_OBJ_NULL());
    }
}
