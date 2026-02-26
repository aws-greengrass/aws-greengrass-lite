// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "helpers.h"
#include <assert.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/flags.h>
#include <gg/io.h>
#include <gg/json_decode.h>
#include <gg/json_encode.h>
#include <gg/list.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <gg/vector.h>
#include <ggconfigd.h>
#include <ggl/core_bus/server.h>
#include <inttypes.h>
#include <time.h>
#include <stdbool.h>

/// Given a GgObject of (possibly nested) GgMaps and/or GgBuffer(s),
/// decode all the GgBuffers from json to their appropriate GGL object types.
// NOLINTNEXTLINE(misc-no-recursion)
static GgError decode_object_destructive(GgObject *obj, GgArena *arena) {
    if (gg_obj_type(*obj) == GG_TYPE_BUF) {
        GgBuffer buf = gg_obj_into_buf(*obj);
        GG_LOGT("given buffer to decode: %.*s", (int) buf.len, buf.data);
        return gg_json_decode_destructive(buf, arena, obj);
    }
    if (gg_obj_type(*obj) == GG_TYPE_MAP) {
        GgMap map = gg_obj_into_map(*obj);
        GG_LOGT("given map to decode with length: %d", (int) map.len);
        GG_MAP_FOREACH (kv, map) {
            GgError decode_err
                = decode_object_destructive(gg_kv_val(kv), arena);
            if (decode_err != GG_ERR_OK) {
                GG_LOGE(
                    "decode map value at index %d and key %.*s failed with error code: %d",
                    (int) (kv - map.pairs),
                    (int) gg_kv_key(*kv).len,
                    gg_kv_key(*kv).data,
                    (int) decode_err
                );
                return decode_err;
            }
        }
        return GG_ERR_OK;
    }

    GG_LOGE("given unexpected type to decode: %d", (int) gg_obj_type(*obj));
    return GG_ERR_FAILURE;
}

static GgError rpc_read(void *ctx, GgMap params, uint32_t handle) {
    (void) ctx;

    GgObject *key_path_obj;
    if (!gg_map_get(params, GG_STR("key_path"), &key_path_obj)
        || (gg_obj_type(*key_path_obj) != GG_TYPE_LIST)) {
        GG_LOGE("read received invalid key_path argument.");
        return GG_ERR_INVALID;
    }
    GgList key_path = gg_obj_into_list(*key_path_obj);

    GgError ret = gg_list_type_check(key_path, GG_TYPE_BUF);
    if (ret != GG_ERR_OK) {
        GG_LOGE("key_path elements must be strings.");
        return GG_ERR_RANGE;
    }

    GG_LOGD("Processing request to read key %s", print_key_path(&key_path));

    GgObject value;
    ret = ggconfig_get_value_from_key(&key_path, &value);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    static uint8_t object_decode_memory[GGCONFIGD_MAX_OBJECT_DECODE_BYTES];
    GgArena object_alloc = gg_arena_init(GG_BUF(object_decode_memory));
    ret = decode_object_destructive(&value, &object_alloc);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ggl_respond(handle, value);
    return GG_ERR_OK;
}

static GgError rpc_list(void *ctx, GgMap params, uint32_t handle) {
    (void) ctx;

    GgObject *key_path_obj;
    if (!gg_map_get(params, GG_STR("key_path"), &key_path_obj)
        || (gg_obj_type(*key_path_obj) != GG_TYPE_LIST)) {
        GG_LOGE("read received invalid key_path argument.");
        return GG_ERR_INVALID;
    }
    GgList key_path = gg_obj_into_list(*key_path_obj);

    GgError ret = gg_list_type_check(key_path, GG_TYPE_BUF);
    if (ret != GG_ERR_OK) {
        GG_LOGE("key_path elements must be strings.");
        return GG_ERR_RANGE;
    }

    GG_LOGD(
        "Processing request to list subkeys of key %s",
        print_key_path(&key_path)
    );

    GgList subkeys;
    ret = ggconfig_list_subkeys(&key_path, &subkeys);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ggl_respond(handle, gg_obj_list(subkeys));
    return GG_ERR_OK;
}

static GgError rpc_delete(void *ctx, GgMap params, uint32_t handle) {
    (void) ctx;

    GgObject *key_path_obj;
    if (!gg_map_get(params, GG_STR("key_path"), &key_path_obj)
        || (gg_obj_type(*key_path_obj) != GG_TYPE_LIST)) {
        GG_LOGE("read received invalid key_path argument.");
        return GG_ERR_INVALID;
    }
    GgList key_path = gg_obj_into_list(*key_path_obj);

    GgError ret = gg_list_type_check(key_path, GG_TYPE_BUF);
    if (ret != GG_ERR_OK) {
        GG_LOGE("key_path elements must be strings.");
        return GG_ERR_RANGE;
    }

    GG_LOGD(
        "Processing request to delete key %s (recursively)",
        print_key_path(&key_path)
    );
    ret = ggconfig_delete_key(&key_path);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ggl_respond(handle, GG_OBJ_NULL);
    return GG_ERR_OK;
}

static GgError rpc_subscribe(void *ctx, GgMap params, uint32_t handle) {
    (void) ctx;

    GgObject *key_path_obj;
    if (!gg_map_get(params, GG_STR("key_path"), &key_path_obj)
        || (gg_obj_type(*key_path_obj) != GG_TYPE_LIST)) {
        GG_LOGE("read received invalid key_path argument.");
        return GG_ERR_INVALID;
    }
    GgList key_path = gg_obj_into_list(*key_path_obj);

    GgError ret = gg_list_type_check(key_path, GG_TYPE_BUF);
    if (ret != GG_ERR_OK) {
        GG_LOGE("key_path elements must be strings.");
        return GG_ERR_RANGE;
    }

    GG_LOGD(
        "Processing request to subscribe handle %" PRIu32 ":%" PRIu32
        " to key %s",
        handle & (0xFFFF0000 >> 16),
        handle & 0x0000FFFF,
        print_key_path(&key_path)
    );

    ret = ggconfig_get_key_notification(&key_path, handle);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ggl_sub_accept(handle, NULL, NULL);
    return GG_ERR_OK;
}

GgError ggconfig_process_nonmap(
    GgObjVec *key_path, GgObject value, int64_t timestamp
) {
    GgBuffer value_buffer = GG_BUF((uint8_t[1024]) { 0 });
    GG_LOGT("Starting json encode.");
    GgBuffer encode_buf = value_buffer;
    GgError error = gg_json_encode(value, gg_buf_writer(&encode_buf));
    if (error != GG_ERR_OK) {
        GG_LOGE(
            "Json encode failed for key %s.", print_key_path(&key_path->list)
        );
        return error;
    }
    value_buffer.len
        = (uintptr_t) encode_buf.data - (uintptr_t) value_buffer.data;
    GG_LOGT("Writing value.");
    error = ggconfig_write_value_at_key(
        &key_path->list, &value_buffer, timestamp
    );
    if (error != GG_ERR_OK) {
        return error;
    }

    GG_LOGT(
        "Wrote %s = %.*s %" PRId64,
        print_key_path(&key_path->list),
        (int) value_buffer.len,
        value_buffer.data,
        timestamp
    );
    return GG_ERR_OK;
}

// TODO: This processing of maps should probably happen in the db_interface
// layer so that merges can be made atomic. Currently it's possible for a subset
// of the writes in a merge to fail while the rest succeed.
// NOLINTNEXTLINE(misc-no-recursion)
GgError ggconfig_process_map(GgObjVec *key_path, GgMap map, int64_t timestamp) {
    GgError ret = GG_ERR_OK;
    if (map.len == 0) {
        GG_LOGT("Map is empty, merging in.");
        return ggconfig_write_empty_map(&key_path->list);
    }
    for (size_t x = 0; x < map.len; x++) {
        GgKV *kv = &map.pairs[x];
        GG_LOGT(
            "Preparing %zu, %.*s",
            x,
            (int) gg_kv_key(*kv).len,
            gg_kv_key(*kv).data
        );

        ret = gg_obj_vec_push(key_path, gg_obj_buf(gg_kv_key(*kv)));
        assert(ret == GG_ERR_OK);
        GG_LOGT("pushed the key");
        if (gg_obj_type(*gg_kv_val(kv)) == GG_TYPE_MAP) {
            GG_LOGT("value is a map");
            GgMap val_map = gg_obj_into_map(*gg_kv_val(kv));
            ret = ggconfig_process_map(key_path, val_map, timestamp);
            if (ret != GG_ERR_OK) {
                break;
            }
        } else {
            GG_LOGT("Value is not a map.");
            ret = ggconfig_process_nonmap(key_path, *gg_kv_val(kv), timestamp);
            if (ret != GG_ERR_OK) {
                break;
            }
        }
        (void) gg_obj_vec_pop(key_path, NULL);
    }
    return ret;
}

static GgError rpc_write(void *ctx, GgMap params, uint32_t handle) {
    (void) ctx;

    GgObject *key_path_obj;
    GgObject *value;
    GgObject *timestamp_obj;
    GgError ret = gg_map_validate(
        params,
        GG_MAP_SCHEMA(
            { GG_STR("key_path"), GG_REQUIRED, GG_TYPE_LIST, &key_path_obj },
            { GG_STR("value"), GG_REQUIRED, GG_TYPE_NULL, &value },
            { GG_STR("timestamp"), GG_OPTIONAL, GG_TYPE_I64, &timestamp_obj },
        )
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("write received one or more invalid arguments.");
        return GG_ERR_INVALID;
    }

    GgList key_path = gg_obj_into_list(*key_path_obj);

    ret = gg_list_type_check(key_path, GG_TYPE_BUF);
    if (ret != GG_ERR_OK) {
        GG_LOGE("key_path elements must be strings.");
        return GG_ERR_RANGE;
    }

    GgObjVec key_path_vec = GG_OBJ_VEC((GgObject[GG_MAX_OBJECT_DEPTH]) { 0 });
    ret = gg_obj_vec_append(&key_path_vec, key_path);
    if (ret != GG_ERR_OK) {
        GG_LOGE("key_path too long.");
        return GG_ERR_RANGE;
    }

    int64_t timestamp;
    if (timestamp_obj != NULL) {
        timestamp = gg_obj_into_i64(*timestamp_obj);
    } else {
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        timestamp = (int64_t) now.tv_sec * 1000 + now.tv_nsec / 1000000;
    }

    GG_LOGD(
        "Processing request to merge a value to key %s with timestamp %" PRId64,
        print_key_path(&key_path_vec.list),
        timestamp
    );

    if (gg_obj_type(*value) == GG_TYPE_MAP) {
        ret = ggconfig_process_map(
            &key_path_vec, gg_obj_into_map(*value), timestamp
        );
        if (ret != GG_ERR_OK) {
            return ret;
        }
    } else {
        ret = ggconfig_process_nonmap(&key_path_vec, *value, timestamp);
        if (ret != GG_ERR_OK) {
            return ret;
        }
    }

    ggl_respond(handle, GG_OBJ_NULL);
    return GG_ERR_OK;
}

void ggconfigd_start_server(void) {
    GglRpcMethodDesc handlers[]
        = { { GG_STR("read"), false, rpc_read, NULL },
            { GG_STR("list"), false, rpc_list, NULL },
            { GG_STR("write"), false, rpc_write, NULL },
            { GG_STR("delete"), false, rpc_delete, NULL },
            { GG_STR("subscribe"), true, rpc_subscribe, NULL } };
    size_t handlers_len = sizeof(handlers) / sizeof(handlers[0]);

    GG_LOGI("Starting listening for requests");
    GgError ret = ggl_listen(GG_STR("gg_config"), handlers, handlers_len);

    GG_LOGE("Exiting with error %u.", (unsigned) ret);
}
