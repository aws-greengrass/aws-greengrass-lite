// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "tesd.h"
#include "token_service.h"
#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/proxy/environment.h>
#include <stdint.h>

GglError run_tesd(void) {
    GglError ret = ggl_proxy_set_environment();
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    static uint8_t rootca_path_mem[512] = { 0 };
    GglArena alloc = ggl_arena_init(GGL_BUF(rootca_path_mem));
    GglBuffer rootca_path;
    ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("rootCaPath")),
        &alloc,
        &rootca_path
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    static uint8_t cert_path_mem[512] = { 0 };
    alloc = ggl_arena_init(GGL_BUF(cert_path_mem));
    GglBuffer cert_path;
    ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("certificateFilePath")),
        &alloc,
        &cert_path
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    static uint8_t key_path_mem[512] = { 0 };
    alloc = ggl_arena_init(GGL_BUF(key_path_mem));
    GglBuffer key_path;
    ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("privateKeyPath")),
        &alloc,
        &key_path
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    static uint8_t thing_name_mem[256] = { 0 };
    alloc = ggl_arena_init(GGL_BUF(thing_name_mem));
    GglBuffer thing_name;
    ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("thingName")),
        &alloc,
        &thing_name
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    static uint8_t role_alias_mem[128] = { 0 };
    alloc = ggl_arena_init(GGL_BUF(role_alias_mem));
    GglBuffer role_alias;
    ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.NucleusLite"),
            GGL_STR("configuration"),
            GGL_STR("iotRoleAlias")
        ),
        &alloc,
        &role_alias
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    static uint8_t cred_endpoint_mem[128] = { 0 };
    alloc = ggl_arena_init(GGL_BUF(cred_endpoint_mem));
    GglBuffer cred_endpoint;
    ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.NucleusLite"),
            GGL_STR("configuration"),
            GGL_STR("iotCredEndpoint")
        ),
        &alloc,
        &cred_endpoint
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = initiate_request(
        rootca_path, cert_path, key_path, thing_name, role_alias, cred_endpoint
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return GGL_ERR_FAILURE;
}
