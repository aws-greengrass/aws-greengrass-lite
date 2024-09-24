// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "tesd.h"
#include "token_service.h"
#include <ggl/buffer.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/object.h>
#include <stdint.h>

GglError run_tesd(void) {
    static uint8_t rootca_path_mem[512] = { 0 };
    GglBuffer rootca_path = GGL_BUF(rootca_path_mem);
    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("rootCaPath")), &rootca_path
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    static uint8_t cert_path_mem[512] = { 0 };
    GglBuffer cert_path = GGL_BUF(cert_path_mem);
    ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("certificateFilePath")),
        &cert_path
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    static uint8_t key_path_mem[512] = { 0 };
    GglBuffer key_path = GGL_BUF(key_path_mem);
    ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("privateKeyPath")), &key_path
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    static uint8_t thing_name_mem[256] = { 0 };
    GglBuffer thing_name = GGL_BUF(thing_name_mem);
    ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("thingName")), &thing_name
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    static uint8_t role_alias_mem[128] = { 0 };
    GglBuffer role_alias = GGL_BUF(role_alias_mem);
    ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.Nucleus-Lite"),
            GGL_STR("configuration"),
            GGL_STR("iotRoleAlias")
        ),
        &role_alias
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    static uint8_t cred_endpoint_mem[128] = { 0 };
    GglBuffer cred_endpoint = GGL_BUF(cred_endpoint_mem);
    ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.Nucleus-Lite"),
            GGL_STR("configuration"),
            GGL_STR("iotCredEndpoint")
        ),
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
