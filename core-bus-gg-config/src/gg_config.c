// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/core_bus/gg_config.h"
#include <ggl/alloc.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <stdint.h>

#define GGL_MAX_CONFIG_DEPTH 10

GglError ggl_gg_config_read(
    GglBuffer *key_path, size_t levels, GglAlloc *alloc, GglObject *result
) {
    if (levels > GGL_MAX_CONFIG_DEPTH) {
        GGL_LOGE("gg_config", "Key path depth exceeds maximum handled.");
        return GGL_ERR_UNSUPPORTED;
    }

    GglObject path_obj[GGL_MAX_CONFIG_DEPTH] = { 0 };
    for (size_t i = 0; i < levels; i++) {
        path_obj[i] = GGL_OBJ(key_path[i]);
    }

    GglMap args = GGL_MAP(
        { GGL_STR("key_path"),
          GGL_OBJ((GglList) { .items = path_obj, .len = levels }) },
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

GglError ggl_gg_config_read_str(
    GglBuffer *key_path, size_t levels, GglBuffer *result
) {
    GglObject result_obj;
    GglBumpAlloc alloc = ggl_bump_alloc_init(*result);

    GglError ret
        = ggl_gg_config_read(key_path, levels, &alloc.alloc, &result_obj);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (result_obj.type != GGL_TYPE_BUF) {
        GGL_LOGE("gg_config", "Configuration value is not a string.");
        return GGL_ERR_CONFIG;
    }

    *result = result_obj.buf;
    return GGL_ERR_OK;
}

GglError ggl_gg_config_write(
    GglBuffer *key_path, size_t levels, GglObject value, int64_t timestamp
) {
    if (timestamp < 0) {
        GGL_LOGE("gg_config", "Timestamp is negative.");
        return GGL_ERR_UNSUPPORTED;
    }

    if (levels > GGL_MAX_CONFIG_DEPTH) {
        GGL_LOGE("gg_config", "Key path depth exceeds maximum handled.");
        return GGL_ERR_UNSUPPORTED;
    }

    GglObject path_obj[GGL_MAX_CONFIG_DEPTH] = { 0 };
    for (size_t i = 0; i < levels; i++) {
        path_obj[i] = GGL_OBJ(key_path[i]);
    }

    GglMap args = GGL_MAP(
        { GGL_STR("key_path"),
          GGL_OBJ((GglList) { .items = path_obj, .len = levels }) },
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
    GglBuffer *key_path,
    size_t levels,
    GglSubscribeCallback on_response,
    GglSubscribeCloseCallback on_close,
    void *ctx,
    uint32_t *handle
) {
    if (levels > GGL_MAX_CONFIG_DEPTH) {
        GGL_LOGE("gg_config", "Key path depth exceeds maximum handled.");
        return GGL_ERR_UNSUPPORTED;
    }

    GglObject path_obj[GGL_MAX_CONFIG_DEPTH] = { 0 };
    for (size_t i = 0; i < levels; i++) {
        path_obj[i] = GGL_OBJ(key_path[i]);
    }

    GglMap args = GGL_MAP(
        { GGL_STR("key_path"),
          GGL_OBJ((GglList) { .items = path_obj, .len = levels }) },
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
