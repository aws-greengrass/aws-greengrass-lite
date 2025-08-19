// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/core_bus/gg_config.h"
#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/list.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <stddef.h>
#include <stdint.h>

GglError ggl_gg_config_read(
    GglBufList key_path, GglArena *alloc, GglObject *result
) {
    if (key_path.len > GGL_MAX_OBJECT_DEPTH) {
        GGL_LOGE("Key path depth exceeds maximum handled.");
        return GGL_ERR_UNSUPPORTED;
    }

    GglObject path_obj[GGL_MAX_OBJECT_DEPTH] = { 0 };
    for (size_t i = 0; i < key_path.len; i++) {
        path_obj[i] = ggl_obj_buf(key_path.bufs[i]);
    }

    GglMap args = GGL_MAP(
        ggl_kv(
            GGL_STR("key_path"),
            ggl_obj_list((GglList) { .items = path_obj, .len = key_path.len })
        ),
    );

    GglError remote_err = GGL_ERR_OK;
    GglError err = ggl_call(
        GGL_STR("gg_config"), GGL_STR("read"), args, &remote_err, alloc, result
    );

    if ((err == GGL_ERR_REMOTE) && (remote_err != GGL_ERR_OK)) {
        err = remote_err;
    }

    return err;
}

GglError ggl_gg_config_list(
    GglBufList key_path, GglArena *alloc, GglList *subkeys_out
) {
    if (key_path.len > GGL_MAX_OBJECT_DEPTH) {
        GGL_LOGE("Key path depth exceeds maximum handled.");
        return GGL_ERR_UNSUPPORTED;
    }

    GglObject path_obj[GGL_MAX_OBJECT_DEPTH] = { 0 };
    for (size_t i = 0; i < key_path.len; i++) {
        path_obj[i] = ggl_obj_buf(key_path.bufs[i]);
    }

    GglMap args = GGL_MAP(
        ggl_kv(
            GGL_STR("key_path"),
            ggl_obj_list((GglList) { .items = path_obj, .len = key_path.len })
        ),
    );

    GglError remote_err = GGL_ERR_FAILURE;
    GglObject result_obj = { 0 };
    GglError err = ggl_call(
        GGL_STR("gg_config"),
        GGL_STR("list"),
        args,
        &remote_err,
        alloc,
        &result_obj
    );
    if ((err == GGL_ERR_REMOTE) && (remote_err != GGL_ERR_OK)) {
        err = remote_err;
    }
    if (ggl_obj_type(result_obj) != GGL_TYPE_LIST) {
        GGL_LOGE("Configuration list failed to return a list.");
        return GGL_ERR_FAILURE;
    }
    GglList result = ggl_obj_into_list(result_obj);
    GglError ret = ggl_list_type_check(result, GGL_TYPE_BUF);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Configuration list returned a non-buffer list object.");
        return GGL_ERR_FAILURE;
    }
    *subkeys_out = result;
    return err;
}

GglError ggl_gg_config_delete(GglBufList key_path) {
    if (key_path.len > GGL_MAX_OBJECT_DEPTH) {
        GGL_LOGE("Key path depth exceeds maximum handled.");
        return GGL_ERR_UNSUPPORTED;
    }

    GglObject path_obj[GGL_MAX_OBJECT_DEPTH] = { 0 };
    for (size_t i = 0; i < key_path.len; i++) {
        path_obj[i] = ggl_obj_buf(key_path.bufs[i]);
    }

    GglMap args = GGL_MAP(
        ggl_kv(
            GGL_STR("key_path"),
            ggl_obj_list((GglList) { .items = path_obj, .len = key_path.len })
        ),
    );

    GglError remote_err = GGL_ERR_OK;
    GglError err = ggl_call(
        GGL_STR("gg_config"), GGL_STR("delete"), args, &remote_err, NULL, NULL
    );

    if ((err == GGL_ERR_REMOTE) && (remote_err != GGL_ERR_OK)) {
        err = remote_err;
    }

    return err;
}

GglError ggl_gg_config_read_str(
    GglBufList key_path, GglArena *alloc, GglBuffer *result
) {
    GglObject result_obj;
    GglError ret = ggl_gg_config_read(key_path, alloc, &result_obj);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (ggl_obj_type(result_obj) != GGL_TYPE_BUF) {
        GGL_LOGE("Configuration value is not a string.");
        return GGL_ERR_CONFIG;
    }

    *result = ggl_obj_into_buf(result_obj);
    return GGL_ERR_OK;
}

GglError ggl_gg_config_write(
    GglBufList key_path, GglObject value, const int64_t *timestamp
) {
    if ((timestamp != NULL) && (*timestamp < 0)) {
        GGL_LOGE("Timestamp is negative.");
        return GGL_ERR_UNSUPPORTED;
    }

    if (key_path.len > GGL_MAX_OBJECT_DEPTH) {
        GGL_LOGE("Key path depth exceeds maximum handled.");
        return GGL_ERR_UNSUPPORTED;
    }

    GglObject path_obj[GGL_MAX_OBJECT_DEPTH] = { 0 };
    for (size_t i = 0; i < key_path.len; i++) {
        path_obj[i] = ggl_obj_buf(key_path.bufs[i]);
    }

    GglMap args = GGL_MAP(
        ggl_kv(
            GGL_STR("key_path"),
            ggl_obj_list((GglList) { .items = path_obj, .len = key_path.len })
        ),
        ggl_kv(GGL_STR("value"), value),
        ggl_kv(
            GGL_STR("timestamp"),
            ggl_obj_i64((timestamp != NULL) ? *timestamp : 0)
        ),
    );
    if (timestamp == NULL) {
        args.len -= 1;
    }

    GglError remote_err = GGL_ERR_OK;
    GglError err = ggl_call(
        GGL_STR("gg_config"), GGL_STR("write"), args, &remote_err, NULL, NULL
    );

    if ((err == GGL_ERR_REMOTE) && (remote_err != GGL_ERR_OK)) {
        err = remote_err;
    }

    return err;
}

GglError ggl_gg_config_subscribe(
    GglBufList key_path,
    GglSubscribeCallback on_response,
    GglSubscribeCloseCallback on_close,
    void *ctx,
    uint32_t *handle
) {
    if (key_path.len > GGL_MAX_OBJECT_DEPTH) {
        GGL_LOGE("Key path depth exceeds maximum handled.");
        return GGL_ERR_UNSUPPORTED;
    }

    GglObject path_obj[GGL_MAX_OBJECT_DEPTH] = { 0 };
    for (size_t i = 0; i < key_path.len; i++) {
        path_obj[i] = ggl_obj_buf(key_path.bufs[i]);
    }

    GglMap args = GGL_MAP(
        ggl_kv(
            GGL_STR("key_path"),
            ggl_obj_list((GglList) { .items = path_obj, .len = key_path.len })
        ),
    );

    GglError remote_err = GGL_ERR_OK;
    GglError err = ggl_subscribe(
        GGL_STR("gg_config"),
        GGL_STR("subscribe"),
        args,
        on_response,
        on_close,
        ctx,
        &remote_err,
        handle
    );

    if ((err == GGL_ERR_REMOTE) && (remote_err != GGL_ERR_OK)) {
        err = remote_err;
    }

    return err;
}
