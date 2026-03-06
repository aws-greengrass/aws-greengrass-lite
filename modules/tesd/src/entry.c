// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "token_service.h"
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/log.h>
#include <gg/types.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/proxy/environment.h>
#include <string.h>
#include <tesd.h>
#include <stdbool.h>
#include <stdint.h>

static GgError cred_config_change_callback(
    void *ctx, uint32_t handle, GgObject data
) {
    (void) ctx;
    (void) handle;
    (void) data;

    tes_update_cred_url();
    return GG_ERR_OK;
}

GgError run_tesd(TesdArgs *args) {
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

    // Role alias: CLI override or config with subscription
    bool role_alias_from_config = (args->role_alias == NULL);
    static uint8_t role_alias_mem[128] = { 0 };
    GgBuffer role_alias;

    if (role_alias_from_config) {
        ret = ggl_gg_config_subscribe(
            GG_BUF_LIST(
                GG_STR("services"),
                GG_STR("aws.greengrass.NucleusLite"),
                GG_STR("configuration"),
                GG_STR("iotRoleAlias")
            ),
            cred_config_change_callback,
            NULL,
            NULL,
            NULL
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to subscribe to iotRoleAlias changes: %d.", ret);
            return ret;
        }

        alloc = gg_arena_init(GG_BUF(role_alias_mem));
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
    } else {
        role_alias = gg_buffer_from_null_term(args->role_alias);
        GG_LOGD("Using CLI override for iotRoleAlias.");
    }

    // Credential endpoint: CLI override or config with subscription
    bool cred_endpoint_from_config = (args->cred_endpoint == NULL);
    static uint8_t cred_endpoint_mem[128] = { 0 };
    GgBuffer cred_endpoint;

    if (cred_endpoint_from_config) {
        ret = ggl_gg_config_subscribe(
            GG_BUF_LIST(
                GG_STR("services"),
                GG_STR("aws.greengrass.NucleusLite"),
                GG_STR("configuration"),
                GG_STR("iotCredEndpoint")
            ),
            cred_config_change_callback,
            NULL,
            NULL,
            NULL
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to subscribe to iotCredEndpoint changes: %d.", ret);
            return ret;
        }

        alloc = gg_arena_init(GG_BUF(cred_endpoint_mem));
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
    } else {
        cred_endpoint = gg_buffer_from_null_term(args->cred_endpoint);
        GG_LOGD("Using CLI override for iotCredEndpoint.");
    }

    GgBuffer interface_name = { 0 };
    if (args->interface_name != NULL) {
        interface_name = gg_buffer_from_null_term(args->interface_name);
    }

    ret = initiate_request(
        rootca_path,
        cert_path,
        key_path,
        thing_name,
        role_alias,
        cred_endpoint,
        interface_name
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    return GG_ERR_FAILURE;
}
