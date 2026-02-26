// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/list.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <ggl/core_bus/client.h>
#include <ggl/core_bus/gg_config.h>
#include <stddef.h>
#include <stdint.h>

GgError ggl_gg_config_read(
    GgBufList key_path, GgArena *alloc, GgObject *result
) {
    if (key_path.len > GG_MAX_OBJECT_DEPTH) {
        GG_LOGE("Key path depth exceeds maximum handled.");
        return GG_ERR_UNSUPPORTED;
    }

    GgObject path_obj[GG_MAX_OBJECT_DEPTH] = { 0 };
    for (size_t i = 0; i < key_path.len; i++) {
        path_obj[i] = gg_obj_buf(key_path.bufs[i]);
    }

    GgMap args = GG_MAP(
        gg_kv(
            GG_STR("key_path"),
            gg_obj_list((GgList) { .items = path_obj, .len = key_path.len })
        ),
    );

    GgError remote_err = GG_ERR_OK;
    GgError err = ggl_call(
        GG_STR("gg_config"), GG_STR("read"), args, &remote_err, alloc, result
    );

    if ((err == GG_ERR_REMOTE) && (remote_err != GG_ERR_OK)) {
        err = remote_err;
    }

    return err;
}

GgError ggl_gg_config_list(
    GgBufList key_path, GgArena *alloc, GgList *subkeys_out
) {
    if (key_path.len > GG_MAX_OBJECT_DEPTH) {
        GG_LOGE("Key path depth exceeds maximum handled.");
        return GG_ERR_UNSUPPORTED;
    }

    GgObject path_obj[GG_MAX_OBJECT_DEPTH] = { 0 };
    for (size_t i = 0; i < key_path.len; i++) {
        path_obj[i] = gg_obj_buf(key_path.bufs[i]);
    }

    GgMap args = GG_MAP(
        gg_kv(
            GG_STR("key_path"),
            gg_obj_list((GgList) { .items = path_obj, .len = key_path.len })
        ),
    );

    GgError remote_err = GG_ERR_FAILURE;
    GgObject result_obj = { 0 };
    GgError err = ggl_call(
        GG_STR("gg_config"),
        GG_STR("list"),
        args,
        &remote_err,
        alloc,
        &result_obj
    );
    if ((err == GG_ERR_REMOTE) && (remote_err != GG_ERR_OK)) {
        err = remote_err;
    }
    if (gg_obj_type(result_obj) != GG_TYPE_LIST) {
        GG_LOGE("Configuration list failed to return a list.");
        return GG_ERR_FAILURE;
    }
    GgList result = gg_obj_into_list(result_obj);
    GgError ret = gg_list_type_check(result, GG_TYPE_BUF);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Configuration list returned a non-buffer list object.");
        return GG_ERR_FAILURE;
    }
    *subkeys_out = result;
    return err;
}

GgError ggl_gg_config_delete(GgBufList key_path) {
    if (key_path.len > GG_MAX_OBJECT_DEPTH) {
        GG_LOGE("Key path depth exceeds maximum handled.");
        return GG_ERR_UNSUPPORTED;
    }

    GgObject path_obj[GG_MAX_OBJECT_DEPTH] = { 0 };
    for (size_t i = 0; i < key_path.len; i++) {
        path_obj[i] = gg_obj_buf(key_path.bufs[i]);
    }

    GgMap args = GG_MAP(
        gg_kv(
            GG_STR("key_path"),
            gg_obj_list((GgList) { .items = path_obj, .len = key_path.len })
        ),
    );

    GgError remote_err = GG_ERR_OK;
    GgError err = ggl_call(
        GG_STR("gg_config"), GG_STR("delete"), args, &remote_err, NULL, NULL
    );

    if ((err == GG_ERR_REMOTE) && (remote_err != GG_ERR_OK)) {
        err = remote_err;
    }

    return err;
}

GgError ggl_gg_config_read_str(
    GgBufList key_path, GgArena *alloc, GgBuffer *result
) {
    GgObject result_obj;
    GgError ret = ggl_gg_config_read(key_path, alloc, &result_obj);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    if (gg_obj_type(result_obj) != GG_TYPE_BUF) {
        GG_LOGE("Configuration value is not a string.");
        return GG_ERR_CONFIG;
    }

    *result = gg_obj_into_buf(result_obj);
    return GG_ERR_OK;
}

GgError ggl_gg_config_write(
    GgBufList key_path, GgObject value, const int64_t *timestamp
) {
    if ((timestamp != NULL) && (*timestamp < 0)) {
        GG_LOGE("Timestamp is negative.");
        return GG_ERR_UNSUPPORTED;
    }

    if (key_path.len > GG_MAX_OBJECT_DEPTH) {
        GG_LOGE("Key path depth exceeds maximum handled.");
        return GG_ERR_UNSUPPORTED;
    }

    GgObject path_obj[GG_MAX_OBJECT_DEPTH] = { 0 };
    for (size_t i = 0; i < key_path.len; i++) {
        path_obj[i] = gg_obj_buf(key_path.bufs[i]);
    }

    GgMap args = GG_MAP(
        gg_kv(
            GG_STR("key_path"),
            gg_obj_list((GgList) { .items = path_obj, .len = key_path.len })
        ),
        gg_kv(GG_STR("value"), value),
        gg_kv(
            GG_STR("timestamp"),
            gg_obj_i64((timestamp != NULL) ? *timestamp : 0)
        ),
    );
    if (timestamp == NULL) {
        args.len -= 1;
    }

    GgError remote_err = GG_ERR_OK;
    GgError err = ggl_call(
        GG_STR("gg_config"), GG_STR("write"), args, &remote_err, NULL, NULL
    );

    if ((err == GG_ERR_REMOTE) && (remote_err != GG_ERR_OK)) {
        err = remote_err;
    }

    return err;
}

GgError ggl_gg_config_subscribe(
    GgBufList key_path,
    GglSubscribeCallback on_response,
    GglSubscribeCloseCallback on_close,
    void *ctx,
    uint32_t *handle
) {
    if (key_path.len > GG_MAX_OBJECT_DEPTH) {
        GG_LOGE("Key path depth exceeds maximum handled.");
        return GG_ERR_UNSUPPORTED;
    }

    GgObject path_obj[GG_MAX_OBJECT_DEPTH] = { 0 };
    for (size_t i = 0; i < key_path.len; i++) {
        path_obj[i] = gg_obj_buf(key_path.bufs[i]);
    }

    GgMap args = GG_MAP(
        gg_kv(
            GG_STR("key_path"),
            gg_obj_list((GgList) { .items = path_obj, .len = key_path.len })
        ),
    );

    GgError remote_err = GG_ERR_OK;
    GgError err = ggl_subscribe(
        GG_STR("gg_config"),
        GG_STR("subscribe"),
        args,
        on_response,
        on_close,
        ctx,
        &remote_err,
        handle
    );

    if ((err == GG_ERR_REMOTE) && (remote_err != GG_ERR_OK)) {
        err = remote_err;
    }

    return err;
}
