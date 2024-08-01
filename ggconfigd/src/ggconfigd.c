// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggconfigd.h"
#include <ggl/core_bus/server.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    GglBuffer component;
    GglBuffer key;
    GglBuffer value;
} ConfigMsg;

static void rpc_read(void *ctx, GglMap params, uint32_t handle) {
    (void) ctx;

    ConfigMsg msg = { 0 };

    GglObject *val;

    if (ggl_map_get(params, GGL_STR("component"), &val)
        && (val->type == GGL_TYPE_BUF)) {
        msg.component = val->buf;
    } else {
        GGL_LOGE("rpc-handler", "read received invalid component argument.");
        ggl_return_err(handle, GGL_ERR_INVALID);
        return;
    }

    if (ggl_map_get(params, GGL_STR("key"), &val)
        && (val->type == GGL_TYPE_BUF)) {
        msg.key = val->buf;
    } else {
        GGL_LOGE("rpc-handler", "read received invalid key argument.");
        ggl_return_err(handle, GGL_ERR_INVALID);
        return;
    }

    // do a sqlite read operation
    // component/key
    // append component & key
    GglBuffer value;

    unsigned long length = msg.component.len + msg.key.len + 1;
    static uint8_t component_buffer[GGCONFIGD_MAX_COMPONENT_SIZE];
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
        // use the data and then free it
        ggl_respond(handle, return_value);
    } else {
        ggl_return_err(handle, GGL_ERR_FAILURE);
    }
}

static void rpc_write(void *ctx, GglMap params, uint32_t handle) {
    (void) ctx;

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
        ggl_return_err(handle, GGL_ERR_INVALID);
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
        ggl_return_err(handle, GGL_ERR_INVALID);
        return;
    }

    if (ggl_map_get(params, GGL_STR("value"), &val)
        && (val->type == GGL_TYPE_BUF)) {
        msg.value = val->buf;
        GGL_LOGI(
            "rpc_write", "value %.*s", (int) msg.key.len, (char *) msg.key.data
        );
    } else {
        GGL_LOGE("rpc-write", "write received invalid value argument.");
        ggl_return_err(handle, GGL_ERR_INVALID);
        return;
    }

    unsigned long length = msg.component.len + msg.key.len + 1;
    static uint8_t component_buffer[GGCONFIGD_MAX_COMPONENT_SIZE];
    GglBuffer component_key;
    component_key.data = component_buffer;
    component_key.len = length;
    memcpy(&component_key.data[0], msg.component.data, msg.component.len);
    component_key.data[msg.component.len] = '/';
    memcpy(
        &component_key.data[msg.component.len + 1], msg.key.data, msg.key.len
    );

    GglError ret = ggconfig_write_value_at_key(&component_key, &msg.value);

    if (ret != GGL_ERR_OK) {
        ggl_return_err(handle, ret);
        return;
    }
    ggl_respond(handle, GGL_OBJ_NULL());
}

static void sub_close_callback(void *ctx, uint32_t handle) {
    (void) ctx;
    (void) handle;
    GGL_LOGD("sub_close_callback", "closing callback for %d", handle);
}

static void rpc_subscribe(void *ctx, GglMap params, uint32_t handle) {
    (void) ctx;

    ConfigMsg msg = { 0 };
    GglObject *val;
    GGL_LOGI("rpc_subscribe", "subscribing");
    if (ggl_map_get(params, GGL_STR("component"), &val)
        && (val->type == GGL_TYPE_BUF)) {
        msg.component = val->buf;
        GGL_LOGI(
            "rpc_subscribe",
            "component %.*s",
            (int) msg.component.len,
            (char *) msg.component.data
        );
    } else {
        GGL_LOGE("rpc_subscribe", "subscribe received invalid component argument.");
        ggl_return_err(handle, GGL_ERR_INVALID);
        return;
    }

    if (ggl_map_get(params, GGL_STR("key"), &val)
        && (val->type == GGL_TYPE_BUF)) {
        msg.key = val->buf;
        GGL_LOGI(
            "rpc_subscribe",
            "key %.*s",
            (int) msg.key.len,
            (char *) msg.key.data
        );
    } else {
        GGL_LOGE("rpc_subscribe", "Received invalid key argument.");
        ggl_return_err(handle, GGL_ERR_INVALID);
        return;
    }
    size_t length = msg.component.len + msg.key.len + 1;
    static uint8_t component_buffer[GGCONFIGD_MAX_COMPONENT_SIZE];
    GglBuffer component_key = ggl_buf_substr(GGL_BUF(component_buffer), 0, length);
    memcpy(&component_key.data[0], msg.component.data, msg.component.len);
    component_key.data[msg.component.len] = '/';
    memcpy(
        &component_key.data[msg.component.len + 1], msg.key.data, msg.key.len
    );

    GglError ret = ggconfig_get_key_notification(&component_key, handle);
    if (ret != GGL_ERR_OK) {
        ggl_return_err(handle, ret);
    }
    ggl_sub_accept(handle, sub_close_callback, NULL);
}

void ggconfigd_start_server(void) {
    GglRpcMethodDesc handlers[]
        = { { GGL_STR("read"), false, rpc_read, NULL },
            { GGL_STR("write"), false, rpc_write, NULL },
            { GGL_STR("subscribe"), true, rpc_subscribe, NULL } };
    size_t handlers_len = sizeof(handlers) / sizeof(handlers[0]);

    ggl_listen(GGL_STR("/aws/ggl/ggconfigd"), handlers, handlers_len);
}
