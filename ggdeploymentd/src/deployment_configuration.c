// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "deployment_configuration.h"
#include <ggl/buffer.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <string.h>
#include <stdint.h>

DeploymentConfiguration config;

GglError read_nucleus_config(GglBuffer config_key, GglBuffer *response) {
    uint8_t resp_mem[128] = { 0 };
    GglBuffer resp = GGL_BUF(resp_mem);

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.NucleusLite"),
            GGL_STR("configuration"),
            config_key
        ),
        &resp
    );

    if (ret != GGL_ERR_OK) {
        GGL_LOGW(
            "Failed to get %.*s from NucleusLite configuration.",
            (int) config_key.len,
            config_key.data
        );
        return ret;
    }

    memcpy(response->data, resp.data, resp.len);
    response->len = resp.len;
    GGL_LOGD("copied info: %.*s", (int) response->len, response->data);
    return GGL_ERR_OK;
}

GglError read_system_config(GglBuffer config_key, GglBuffer *response) {
    uint8_t resp_mem[128] = { 0 };
    GglBuffer resp = GGL_BUF(resp_mem);

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), config_key), &resp
    );

    if (ret != GGL_ERR_OK) {
        GGL_LOGW(
            "Failed to get %.*s from system configuration.",
            (int) config_key.len,
            config_key.data
        );
        return ret;
    }

    memcpy(response->data, resp.data, resp.len);
    response->len = resp.len;
    GGL_LOGD("copied info: %.*s", (int) response->len, response->data);
    return GGL_ERR_OK;
}

GglError get_posix_user(GglBuffer *posix_user) {
    uint8_t resp_mem[128] = { 0 };
    GglBuffer resp = GGL_BUF(resp_mem);

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.NucleusLite"),
            GGL_STR("configuration"),
            GGL_STR("runWithDefault"),
            GGL_STR("posixUser")
        ),
        &resp
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW("Failed to get posixUser from config.");
        return ret;
    }

    memcpy(posix_user->data, resp.data, resp.len);
    posix_user->len = resp.len;
    GGL_LOGD("copied info: %.*s", (int) posix_user->len, posix_user->data);
    return GGL_ERR_OK;
}
