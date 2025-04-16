// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggconfigd.h"
#include "helpers.h"
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/constants.h>
#include <ggl/core_bus/server.h>
#include <ggl/error.h>
#include <ggl/json_decode.h>
#include <ggl/json_encode.h>
#include <ggl/list.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <inttypes.h>
#include <time.h>
#include <stdbool.h>

/// Given a GglObject of (possibly nested) GglMaps and/or GglBuffer(s),
/// decode all the GglBuffers from json to their appropriate GGL object types.
// NOLINTNEXTLINE(misc-no-recursion)
static GglError decode_object_destructive(
    GglObject *obj, GglBumpAlloc *bump_alloc
) {
    GglError return_err = GGL_ERR_FAILURE;
    if (ggl_obj_type(*obj) == GGL_TYPE_BUF) {
        GglBuffer obj_buf = ggl_obj_into_buf(*obj);
        GGL_LOGT(
            "given buffer to decode: %.*s", (int) obj_buf.len, obj_buf.data
        );
        GglError json_decode_err
            = ggl_json_decode_destructive(obj_buf, &(bump_alloc->alloc), obj);
        if (json_decode_err != GGL_ERR_OK) {
            GGL_LOGE(
                "decode json failed with error code: %d", (int) json_decode_err
            );
            return GGL_ERR_FAILURE;
        }
    } else if (ggl_obj_type(*obj) == GGL_TYPE_MAP) {
        GglMap obj_map = ggl_obj_into_map(*obj);
        GGL_LOGT("given map to decode with length: %d", (int) obj_map.len);
        GGL_MAP_FOREACH(kv, obj_map) {
            GglError decode_err
                = decode_object_destructive(&kv->val, bump_alloc);
            if (decode_err != GGL_ERR_OK) {
                GGL_LOGE(
                    "decode map value at index %d and key %.*s failed with "
                    "error code: %d",
                    (int) (kv - obj_map.pairs),
                    (int) kv->key.len,
                    kv->key.data,
                    (int) decode_err
                );
                return decode_err;
            }
        }
        return_err = GGL_ERR_OK;
    } else {
        GGL_LOGE(
            "given unexpected type to decode: %d", (int) ggl_obj_type(*obj)
        );
        return_err = GGL_ERR_FAILURE;
    }
    return return_err;
}

static GglError rpc_read(void *ctx, GglMap params, uint32_t handle) {
    (void) ctx;

    GglObject *key_path_obj;
    if (!ggl_map_get(params, GGL_STR("key_path"), &key_path_obj)
        || (ggl_obj_type(*key_path_obj) != GGL_TYPE_LIST)) {
        GGL_LOGE("read received invalid key_path argument.");
        return GGL_ERR_INVALID;
    }
    GglList key_path = ggl_obj_into_list(*key_path_obj);

    GglError ret = ggl_list_type_check(key_path, GGL_TYPE_BUF);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("key_path elements must be strings.");
        return GGL_ERR_RANGE;
    }

    GGL_LOGD("Processing request to read key %s", print_key_path(&key_path));

    GglObject value;
    ret = ggconfig_get_value_from_key(&key_path, &value);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    static uint8_t object_decode_memory[GGCONFIGD_MAX_OBJECT_DECODE_BYTES];
    GglBumpAlloc object_alloc
        = ggl_bump_alloc_init(GGL_BUF(object_decode_memory));
    decode_object_destructive(&value, &object_alloc);

    ggl_respond(handle, value);
    return GGL_ERR_OK;
}

static GglError rpc_list(void *ctx, GglMap params, uint32_t handle) {
    (void) ctx;

    GglObject *key_path_obj;
    if (!ggl_map_get(params, GGL_STR("key_path"), &key_path_obj)
        || (ggl_obj_type(*key_path_obj) != GGL_TYPE_LIST)) {
        GGL_LOGE("read received invalid key_path argument.");
        return GGL_ERR_INVALID;
    }
    GglList key_path = ggl_obj_into_list(*key_path_obj);

    GglError ret = ggl_list_type_check(key_path, GGL_TYPE_BUF);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("key_path elements must be strings.");
        return GGL_ERR_RANGE;
    }

    GGL_LOGD(
        "Processing request to list subkeys of key %s",
        print_key_path(&key_path)
    );

    GglList subkeys;
    ret = ggconfig_list_subkeys(&key_path, &subkeys);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ggl_respond(handle, ggl_obj_list(subkeys));
    return GGL_ERR_OK;
}

static GglError rpc_delete(void *ctx, GglMap params, uint32_t handle) {
    (void) ctx;

    GglObject *key_path_obj;
    if (!ggl_map_get(params, GGL_STR("key_path"), &key_path_obj)
        || (ggl_obj_type(*key_path_obj) != GGL_TYPE_LIST)) {
        GGL_LOGE("read received invalid key_path argument.");
        return GGL_ERR_INVALID;
    }
    GglList key_path = ggl_obj_into_list(*key_path_obj);

    GglError ret = ggl_list_type_check(key_path, GGL_TYPE_BUF);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("key_path elements must be strings.");
        return GGL_ERR_RANGE;
    }

    GGL_LOGD(
        "Processing request to delete key %s (recursively)",
        print_key_path(&key_path)
    );
    ret = ggconfig_delete_key(&key_path);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ggl_respond(handle, GGL_OBJ_NULL());
    return GGL_ERR_OK;
}

static GglError rpc_subscribe(void *ctx, GglMap params, uint32_t handle) {
    (void) ctx;

    GglObject *key_path_obj;
    if (!ggl_map_get(params, GGL_STR("key_path"), &key_path_obj)
        || (ggl_obj_type(*key_path_obj) != GGL_TYPE_LIST)) {
        GGL_LOGE("read received invalid key_path argument.");
        return GGL_ERR_INVALID;
    }
    GglList key_path = ggl_obj_into_list(*key_path_obj);

    GglError ret = ggl_list_type_check(key_path, GGL_TYPE_BUF);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("key_path elements must be strings.");
        return GGL_ERR_RANGE;
    }

    GGL_LOGD(
        "Processing request to subscribe handle %" PRIu32 ":%" PRIu32
        " to key %s",
        handle & (0xFFFF0000 >> 16),
        handle & 0x0000FFFF,
        print_key_path(&key_path)
    );

    ret = ggconfig_get_key_notification(&key_path, handle);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ggl_sub_accept(handle, NULL, NULL);
    return GGL_ERR_OK;
}

GglError ggconfig_process_nonmap(
    GglObjVec *key_path, GglObject value, int64_t timestamp
) {
    uint8_t value_string[1024] = { 0 };
    GglBuffer value_buffer
        = { .data = value_string, .len = sizeof(value_string) };
    GGL_LOGT("Starting json encode.");
    GglError error = ggl_json_encode(value, &value_buffer);
    if (error != GGL_ERR_OK) {
        GGL_LOGE(
            "Json encode failed for key %s.", print_key_path(&key_path->list)
        );
        return error;
    }
    GGL_LOGT("Writing value.");
    error = ggconfig_write_value_at_key(
        &key_path->list, &value_buffer, timestamp
    );
    if (error != GGL_ERR_OK) {
        return error;
    }

    GGL_LOGT(
        "Wrote %s = %.*s %" PRId64,
        print_key_path(&key_path->list),
        (int) value_buffer.len,
        value_buffer.data,
        timestamp
    );
    return GGL_ERR_OK;
}

// TODO: This processing of maps should probably happen in the db_interface
// layer so that merges can be made atomic. Currently it's possible for a subset
// of the writes in a merge to fail while the rest succeed.
// NOLINTNEXTLINE(misc-no-recursion)
GglError ggconfig_process_map(
    GglObjVec *key_path, GglMap map, int64_t timestamp
) {
    GglError error = GGL_ERR_OK;
    if (map.len == 0) {
        GGL_LOGT("Map is empty, merging in.");
        return ggconfig_write_empty_map(&key_path->list);
    }
    for (size_t x = 0; x < map.len; x++) {
        GglKV *kv = &map.pairs[x];
        GGL_LOGT("Preparing %zu, %.*s", x, (int) kv->key.len, kv->key.data);

        ggl_obj_vec_push(key_path, ggl_obj_buf(kv->key));
        GGL_LOGT("pushed the key");
        if (ggl_obj_type(kv->val) == GGL_TYPE_MAP) {
            GGL_LOGT("value is a map");
            GglMap val_map = ggl_obj_into_map(kv->val);
            error = ggconfig_process_map(key_path, val_map, timestamp);
            if (error != GGL_ERR_OK) {
                break;
            }
        } else {
            GGL_LOGT("Value is not a map.");
            error = ggconfig_process_nonmap(key_path, kv->val, timestamp);
            if (error != GGL_ERR_OK) {
                break;
            }
        }
        ggl_obj_vec_pop(key_path, NULL);
    }
    return error;
}

static GglError rpc_write(void *ctx, GglMap params, uint32_t handle) {
    (void) ctx;

    GglObject *key_path_obj;
    GglObject *value;
    GglObject *timestamp_obj;
    GglError ret = ggl_map_validate(
        params,
        GGL_MAP_SCHEMA(
            { GGL_STR("key_path"), true, GGL_TYPE_LIST, &key_path_obj },
            { GGL_STR("value"), true, GGL_TYPE_NULL, &value },
            { GGL_STR("timestamp"), false, GGL_TYPE_I64, &timestamp_obj },
        )
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("write received one or more invalid arguments.");
        return GGL_ERR_INVALID;
    }

    GglList key_path = ggl_obj_into_list(*key_path_obj);

    ret = ggl_list_type_check(key_path, GGL_TYPE_BUF);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("key_path elements must be strings.");
        return GGL_ERR_RANGE;
    }

    GglObjVec key_path_vec
        = GGL_OBJ_VEC((GglObject[GGL_MAX_OBJECT_DEPTH]) { 0 });
    ret = ggl_obj_vec_append(&key_path_vec, key_path);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("key_path too long.");
        return GGL_ERR_RANGE;
    }

    int64_t timestamp;
    if (timestamp_obj != NULL) {
        timestamp = ggl_obj_into_i64(*timestamp_obj);
    } else {
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        timestamp = (int64_t) now.tv_sec * 1000 + now.tv_nsec / 1000000;
    }

    GGL_LOGD(
        "Processing request to merge a value to key %s with timestamp %" PRId64,
        print_key_path(&key_path_vec.list),
        timestamp
    );

    if (ggl_obj_type(*value) == GGL_TYPE_MAP) {
        ret = ggconfig_process_map(
            &key_path_vec, ggl_obj_into_map(*value), timestamp
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    } else {
        ret = ggconfig_process_nonmap(&key_path_vec, *value, timestamp);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    ggl_respond(handle, GGL_OBJ_NULL());
    return GGL_ERR_OK;
}

void ggconfigd_start_server(void) {
    GglRpcMethodDesc handlers[]
        = { { GGL_STR("read"), false, rpc_read, NULL },
            { GGL_STR("list"), false, rpc_list, NULL },
            { GGL_STR("write"), false, rpc_write, NULL },
            { GGL_STR("delete"), false, rpc_delete, NULL },
            { GGL_STR("subscribe"), true, rpc_subscribe, NULL } };
    size_t handlers_len = sizeof(handlers) / sizeof(handlers[0]);

    GGL_LOGI("Starting listening for requests");
    ggl_listen(GGL_STR("gg_config"), handlers, handlers_len);
}
