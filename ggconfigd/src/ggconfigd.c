/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ggconfig.h"
#include "ggl/buffer.h"
#include "ggl/error.h"
#include "ggl/log.h"
#include "ggl/map.h"
#include "ggl/object.h"
#include "ggl/server.h"
#include <string.h>
#include <stdint.h>

#define MAX_COMPONENT_SIZE 1024
#define MAX_KEY_SIZE 1024
#define MAX_VALUE_SIZE 1024

typedef struct {
    GglBuffer component;
    GglBuffer key;
    GglBuffer value;
} ConfigMsg;

static void rpc_read(GglMap params, GglResponseHandle *handle) {
    ConfigMsg msg = { 0 };

    GglObject *val;

    if (ggl_map_get(params, GGL_STR("component"), &val)
        && (val->type == GGL_TYPE_BUF)) {
        msg.component = val->buf;
    } else {
        GGL_LOGE("rpc-handler", "read received invalid component argument.");
        ggl_respond(handle, GGL_ERR_INVALID, GGL_OBJ_NULL());
        return;
    }

    if (ggl_map_get(params, GGL_STR("key"), &val)
        && (val->type == GGL_TYPE_BUF)) {
        msg.key = val->buf;
    } else {
        GGL_LOGE("rpc-handler", "read received invalid key argument.");
        ggl_respond(handle, GGL_ERR_INVALID, GGL_OBJ_NULL());
        return;
    }

    /* do a sqlite read operation */
    /* component/key */
    /* append component & key */
    GglBuffer value;

    unsigned long length = msg.component.len + msg.key.len + 1;
    static uint8_t component_buffer[MAX_COMPONENT_SIZE];
    GglBuffer component_key;
    component_key.data = component_buffer;
    component_key.len = length;
    memcpy(&component_key.data[0], msg.component.data, msg.component.len);
    component_key.data[msg.component.len] = '/';
    memcpy(
        &component_key.data[msg.component.len + 1], msg.key.data, msg.key.len
    );

    if (ggconfig_get_value_from_key(&component_key, &value) == GGL_ERR_OK) {
        GglObject return_value = { .type = GGL_TYPE_BUF, .buf = value };
        /* use the data and then free it*/
        ggl_respond(handle, GGL_ERR_OK, return_value);
    } else {
        ggl_respond(handle, GGL_ERR_FAILURE, GGL_OBJ_NULL());
    }
}

static void rpc_write(GglMap params, GglResponseHandle *handle) {
    ConfigMsg msg = { 0 };

    GglObject *val;

    if (ggl_map_get(params, GGL_STR("component"), &val)
        && (val->type == GGL_TYPE_BUF)) {
        msg.component = val->buf;
        GGL_LOGI(
            "rpc_write",
            "component %.*s",
            (int) msg.component.len,
            (char *) msg.component.data
        );
    } else {
        GGL_LOGE("rpc-handler", "write received invalid component argument.");
        ggl_respond(handle, GGL_ERR_INVALID, GGL_OBJ_NULL());
        return;
    }

    if (ggl_map_get(params, GGL_STR("key"), &val)
        && (val->type == GGL_TYPE_BUF)) {
        msg.key = val->buf;
        GGL_LOGI(
            "rpc_write", "key %.*s", (int) msg.key.len, (char *) msg.key.data
        );
    } else {
        GGL_LOGE("rpc-handler", "write received invalid key argument.");
        ggl_respond(handle, GGL_ERR_INVALID, GGL_OBJ_NULL());
        return;
    }

    if (ggl_map_get(params, GGL_STR("value"), &val)
        && (val->type == GGL_TYPE_BUF)) {
        msg.value = val->buf;
        GGL_LOGI(
            "rpc_write", "value %.*s", (int) msg.key.len, (char *) msg.key.data
        );
    } else {
        GGL_LOGE("rpc-handler", "write received invalid value argument.");
        ggl_respond(handle, GGL_ERR_INVALID, GGL_OBJ_NULL());
        return;
    }

    unsigned long length = msg.component.len + msg.key.len + 1;
    static uint8_t component_buffer[MAX_COMPONENT_SIZE];
    GglBuffer component_key;
    component_key.data = component_buffer;
    component_key.len = length;
    memcpy(&component_key.data[0], msg.component.data, msg.component.len);
    component_key.data[msg.component.len] = '/';
    memcpy(
        &component_key.data[msg.component.len + 1], msg.key.data, msg.key.len
    );

    GglError ret = ggconfig_write_value_at_key(&component_key, &msg.value);

    ggl_respond(handle, ret, GGL_OBJ_NULL());
}

void ggl_receive_callback(
    void *ctx, GglBuffer method, GglMap params, GglResponseHandle *handle
) {
    (void) ctx;

    if (ggl_buffer_eq(method, GGL_STR("write"))) {
        rpc_write(params, handle);
    } else if (ggl_buffer_eq(method, GGL_STR("read"))) {
        rpc_read(params, handle);
    } else {
        GGL_LOGE(
            "rpc-handler",
            "Received unknown command: %.*s.",
            (int) method.len,
            method.data
        );
        ggl_respond(handle, GGL_ERR_INVALID, GGL_OBJ_NULL());
    }
}
