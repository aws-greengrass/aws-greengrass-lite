// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggconfigd.h"
#include <ggl/core_bus/server.h>
#include <ggl/error.h>
#include <ggl/json_encode.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_KEY_PATH_DEPTH 25

typedef struct {
    GglBuffer component;
    GglBuffer key;
    GglBuffer value;
} ConfigMsg;

static void rpc_read(void *ctx, GglMap params, uint32_t handle) {
    (void) ctx;

    GglObject *val;
    GglList *key_list;

    if (ggl_map_get(params, GGL_STR("keyPath"), &val)
        && (val->type == GGL_TYPE_LIST)) {
        key_list = &val->list;
    } else {
        GGL_LOGE("rpc_read", "read received invalid keyPath argument.");
        ggl_return_err(handle, GGL_ERR_INVALID);
        return;
    }

    GglBuffer value;

    if (ggconfig_get_value_from_key(key_list, &value) == GGL_ERR_OK) {
        GglObject return_value = { .type = GGL_TYPE_BUF, .buf = value };
        // use the data and then free it
        ggl_respond(handle, return_value);
    } else {
        ggl_return_err(handle, GGL_ERR_FAILURE);
    }
}

static void sub_close_callback(void *ctx, uint32_t handle) {
    (void) ctx;
    (void) handle;
    GGL_LOGD("sub_close_callback", "closing callback for %d", handle);
}

static void rpc_subscribe(void *ctx, GglMap params, uint32_t handle) {
    (void) ctx;

    GglObject *val;
    GglList *key_list;

    GGL_LOGI("rpc_subscribe", "subscribing");

    if (ggl_map_get(params, GGL_STR("keyPath"), &val)
        && (val->type == GGL_TYPE_LIST)) {
        key_list = &val->list;
    } else {
        GGL_LOGE("rpc_subscribe", "read received invalid keyPath argument.");
        ggl_return_err(handle, GGL_ERR_INVALID);
        return;
    }

    GglError ret = ggconfig_get_key_notification(key_list, handle);
    if (ret != GGL_ERR_OK) {
        ggl_return_err(handle, ret);
    }
    ggl_sub_accept(handle, sub_close_callback, NULL);
}

static char *print_key_path(GglList *key_path) {
    static char path_string[64] = { 0 };
    memset(path_string, 0, sizeof(path_string));
    for (size_t x = 0; x < key_path->len; x++) {
        if (x > 0) {
            strncat(path_string, "/ ", 1);
        }
        strncat(
            path_string,
            (char *) key_path->items[x].buf.data,
            key_path->items[x].buf.len
        );
    }
    return path_string;
}

// NOLINTNEXTLINE(misc-no-recursion)
static GglError process_map(
    GglObjVec *key_path, GglMap *the_map, long time_stamp
) {
    GglError error = GGL_ERR_OK;
    for (size_t x = 0; x < the_map->len; x++) {
        GglKV *kv = &the_map->pairs[x];
        ggl_obj_vec_push(key_path, GGL_OBJ(kv->key));
        if (kv->val.type == GGL_TYPE_MAP) {
            error = process_map(key_path, &kv->val.map, time_stamp);
            if (error != GGL_ERR_OK) {
                break;
            }
        } else {
            char *path_string = print_key_path(&key_path->list);
            uint8_t value_string[512] = { 0 };
            GglBuffer value_buffer
                = { .data = value_string, .len = sizeof(value_string) };
            error = ggl_json_encode(kv->val, &value_buffer);
            if (error != GGL_ERR_OK) {
                break;
            }
            error = ggconfig_write_value_at_key(&key_path->list, &value_buffer);

            GGL_LOGT(
                "rpc_write:process_map",
                "writing %s = %.*s %ld",
                path_string,
                (int) value_buffer.len,
                (char *) value_buffer.data,
                time_stamp
            );
        }
        ggl_obj_vec_pop(key_path, NULL);
    }
    return error;
}

static void rpc_write(void *ctx, GglMap params, uint32_t handle) {
    /// Receive the following parameters
    int64_t time_stamp = 1;
    (void) ctx;

    GglObject *val;
    GglObject object_list_memory[MAX_KEY_PATH_DEPTH] = { 0 };
    GglList object_list = { .items = object_list_memory, .len = 0 };
    GglObjVec key_path
        = { .list = object_list, .capacity = MAX_KEY_PATH_DEPTH };

    if (ggl_map_get(params, GGL_STR("keyPath"), &val)
        && (val->type == GGL_TYPE_LIST)) {
        GglList *list = &val->list;
        for (size_t x = 0; x < list->len; x++) {
            if (ggl_obj_vec_push(&key_path, list->items[x]) != GGL_ERR_OK) {
                GGL_LOGE("rpc_write", "Error pushing to the keypath");
                ggl_return_err(handle, GGL_ERR_INVALID);
                return;
            }
        }
    } else {
        GGL_LOGE("rpc_write", "write received invalid keyPath argument.");
        ggl_return_err(handle, GGL_ERR_INVALID);
        return;
    }

    if (ggl_map_get(params, GGL_STR("timestamp"), &val)
        && (val->type == GGL_TYPE_I64)) {
        GGL_LOGI("rpc_write", "timeStamp %ld", val->i64);
    } else {
        time_stamp = 1; // TODO make a better default
    }

    if (ggl_map_get(params, GGL_STR("valueToMerge"), &val)
        && (val->type == GGL_TYPE_MAP)) {
        GGL_LOGT("rpc_write", "valueToMerge is a Map");
        GglError error = process_map(&key_path, &val->map, time_stamp);
        if (error != GGL_ERR_OK) {
            ggl_return_err(handle, error);
        } else {
            ggl_respond(handle, GGL_OBJ_NULL());
        }
        return;
    }
    GGL_LOGE("rpc_write", "write received invalid value argument.");
    ggl_return_err(handle, GGL_ERR_INVALID);
}

void ggconfigd_start_server(void) {
    GglRpcMethodDesc handlers[]
        = { { GGL_STR("read"), false, rpc_read, NULL },
            { GGL_STR("write"), false, rpc_write, NULL },
            { GGL_STR("subscribe"), true, rpc_subscribe, NULL } };
    size_t handlers_len = sizeof(handlers) / sizeof(handlers[0]);

    ggl_listen(GGL_STR("/aws/ggl/ggconfigd"), handlers, handlers_len);
}
