// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/core_bus/gg_config.h"
#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/constants.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <stddef.h>
#include <stdint.h>

GglError ggl_gg_config_read(
    GglBufList key_path, GglAlloc *alloc, GglObject *result
) {
    if (key_path.len > GGL_MAX_OBJECT_DEPTH) {
        GGL_LOGE("Key path depth exceeds maximum handled.");
        return GGL_ERR_UNSUPPORTED;
    }

    GglObject path_obj[GGL_MAX_OBJECT_DEPTH] = { 0 };
    for (size_t i = 0; i < key_path.len; i++) {
        path_obj[i] = GGL_OBJ_BUF(key_path.bufs[i]);
    }

    GglMap args = GGL_MAP(
        { GGL_STR("key_path"),
          GGL_OBJ_LIST((GglList) { .items = path_obj, .len = key_path.len }) },
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

GglError ggl_gg_config_read_str(GglBufList key_path, GglBuffer *result) {
    GglObject result_obj;
    GglBumpAlloc alloc = ggl_bump_alloc_init(*result);

    GglError ret = ggl_gg_config_read(key_path, &alloc.alloc, &result_obj);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (result_obj.type != GGL_TYPE_BUF) {
        GGL_LOGE("Configuration value is not a string.");
        return GGL_ERR_CONFIG;
    }

    *result = result_obj.buf;
    return GGL_ERR_OK;
}

GglError ggl_gg_config_write(
    GglBufList key_path, GglObject value, int64_t timestamp
) {
    if (timestamp < 0) {
        GGL_LOGE("Timestamp is negative.");
        return GGL_ERR_UNSUPPORTED;
    }

    if (key_path.len > GGL_MAX_OBJECT_DEPTH) {
        GGL_LOGE("Key path depth exceeds maximum handled.");
        return GGL_ERR_UNSUPPORTED;
    }

    GglObject path_obj[GGL_MAX_OBJECT_DEPTH] = { 0 };
    for (size_t i = 0; i < key_path.len; i++) {
        path_obj[i] = GGL_OBJ_BUF(key_path.bufs[i]);
    }

    GglMap args = GGL_MAP(
        { GGL_STR("key_path"),
          GGL_OBJ_LIST((GglList) { .items = path_obj, .len = key_path.len }) },
        { GGL_STR("value"), value },
        { GGL_STR("timestamp"), GGL_OBJ_I64(timestamp) },
    );

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
        path_obj[i] = GGL_OBJ_BUF(key_path.bufs[i]);
    }

    GglMap args = GGL_MAP(
        { GGL_STR("key_path"),
          GGL_OBJ_LIST((GglList) { .items = path_obj, .len = key_path.len }) },
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
