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
#include <tesd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

static GgError cred_config_change_callback(
    void *ctx, uint32_t handle, GgObject data
) {
    (void) handle;
    (void) data;
    const char *key = (const char *) ctx;
    GG_LOGI("Configuration key '%s' changed, restarting tesd.", key);
    _Exit(0);
}

// Subscribe to all config keys that affect credential fetches. When any of
// these change, tesd exits so systemd restarts it with fresh config.
//
// Keys monitored:
//   iotCredEndpoint     - HTTPS endpoint for the IoT Credential Provider
//   iotRoleAlias        - IAM role alias used in the credential request URL
//   thingName           - sent as x-amzn-iot-thingname header in credential req
//   certificateFilePath - client cert for mTLS to the credential provider
//   privateKeyPath      - private key for mTLS to the credential provider
//   rootCaPath          - CA cert for TLS verification of credential provider
static GgError subscribe_to_cred_config_changes(TesdArgs *args) {
    GgError ret = ggl_gg_config_subscribe(
        GG_BUF_LIST(GG_STR("system"), GG_STR("thingName")),
        cred_config_change_callback,
        NULL,
        (void *) "thingName",
        NULL
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to subscribe to thingName changes: %d.", ret);
        return ret;
    }

    ret = ggl_gg_config_subscribe(
        GG_BUF_LIST(GG_STR("system"), GG_STR("certificateFilePath")),
        cred_config_change_callback,
        NULL,
        (void *) "certificateFilePath",
        NULL
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to subscribe to certificateFilePath changes: %d.", ret);
        return ret;
    }

    ret = ggl_gg_config_subscribe(
        GG_BUF_LIST(GG_STR("system"), GG_STR("rootCaPath")),
        cred_config_change_callback,
        NULL,
        (void *) "rootCaPath",
        NULL
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to subscribe to rootCaPath changes: %d.", ret);
        return ret;
    }

    ret = ggl_gg_config_subscribe(
        GG_BUF_LIST(GG_STR("system"), GG_STR("privateKeyPath")),
        cred_config_change_callback,
        NULL,
        (void *) "privateKeyPath",
        NULL
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to subscribe to privateKeyPath changes: %d.", ret);
        return ret;
    }

    if (args->role_alias == NULL) {
        ret = ggl_gg_config_subscribe(
            GG_BUF_LIST(
                GG_STR("services"),
                GG_STR("aws.greengrass.NucleusLite"),
                GG_STR("configuration"),
                GG_STR("iotRoleAlias")
            ),
            cred_config_change_callback,
            NULL,
            (void *) "iotRoleAlias",
            NULL
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to subscribe to iotRoleAlias changes: %d.", ret);
            return ret;
        }
    }

    if (args->cred_endpoint == NULL) {
        ret = ggl_gg_config_subscribe(
            GG_BUF_LIST(
                GG_STR("services"),
                GG_STR("aws.greengrass.NucleusLite"),
                GG_STR("configuration"),
                GG_STR("iotCredEndpoint")
            ),
            cred_config_change_callback,
            NULL,
            (void *) "iotCredEndpoint",
            NULL
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to subscribe to iotCredEndpoint changes: %d.", ret);
            return ret;
        }
    }

    return GG_ERR_OK;
}

GgError run_tesd(TesdArgs *args) {
    GgError ret = ggl_proxy_set_environment();
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ret = subscribe_to_cred_config_changes(args);
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

    // Role alias: CLI override or config
    bool role_alias_from_config = (args->role_alias == NULL);
    static uint8_t role_alias_mem[128] = { 0 };
    GgBuffer role_alias;

    if (role_alias_from_config) {
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

    // Credential endpoint: CLI override or config
    bool cred_endpoint_from_config = (args->cred_endpoint == NULL);
    static uint8_t cred_endpoint_mem[128] = { 0 };
    GgBuffer cred_endpoint;

    if (cred_endpoint_from_config) {
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
