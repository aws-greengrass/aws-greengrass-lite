// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "token_service.h"
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/types.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/proxy/environment.h>
#include <tesd.h>
#include <stdint.h>

GgError run_tesd(void) {
    GgError ret = ggl_proxy_set_environment();
    if (ret != GG_ERR_OK) {
        return ret;
    }

    static uint8_t rootca_path_mem[512] = { 0 };
    GgArena alloc = gg_arena_init(GG_BUF(rootca_path_mem));
    GgBuffer rootca_path;
    ret = ggl_gg_config_read_str(
        GG_BUF_LIST(GG_STR("system"), GG_STR("rootCaPath")),
        &alloc,
        &rootca_path
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    static uint8_t cert_path_mem[512] = { 0 };
    alloc = gg_arena_init(GG_BUF(cert_path_mem));
    GgBuffer cert_path;
    ret = ggl_gg_config_read_str(
        GG_BUF_LIST(GG_STR("system"), GG_STR("certificateFilePath")),
        &alloc,
        &cert_path
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    static uint8_t key_path_mem[512] = { 0 };
    alloc = gg_arena_init(GG_BUF(key_path_mem));
    GgBuffer key_path;
    ret = ggl_gg_config_read_str(
        GG_BUF_LIST(GG_STR("system"), GG_STR("privateKeyPath")),
        &alloc,
        &key_path
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    static uint8_t thing_name_mem[256] = { 0 };
    alloc = gg_arena_init(GG_BUF(thing_name_mem));
    GgBuffer thing_name;
    ret = ggl_gg_config_read_str(
        GG_BUF_LIST(GG_STR("system"), GG_STR("thingName")), &alloc, &thing_name
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    static uint8_t role_alias_mem[128] = { 0 };
    alloc = gg_arena_init(GG_BUF(role_alias_mem));
    GgBuffer role_alias;
    ret = ggl_gg_config_read_str(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("aws.greengrass.NucleusLite"),
            GG_STR("configuration"),
            GG_STR("iotRoleAlias")
        ),
        &alloc,
        &role_alias
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    static uint8_t cred_endpoint_mem[128] = { 0 };
    alloc = gg_arena_init(GG_BUF(cred_endpoint_mem));
    GgBuffer cred_endpoint;
    ret = ggl_gg_config_read_str(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("aws.greengrass.NucleusLite"),
            GG_STR("configuration"),
            GG_STR("iotCredEndpoint")
        ),
        &alloc,
        &cred_endpoint
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ret = initiate_request(
        rootca_path, cert_path, key_path, thing_name, role_alias, cred_endpoint
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    return GG_ERR_FAILURE;
}
