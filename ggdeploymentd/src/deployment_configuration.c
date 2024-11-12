// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "deployment_configuration.h"
#include <ggl/buffer.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <stdint.h>

DeploymentConfiguration config;

GglError get_data_endpoint(GglByteVec *endpoint) {
    static uint8_t resp_mem[128] = { 0 };
    GglBuffer resp = GGL_BUF(resp_mem);

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.Nucleus-Lite"),
            GGL_STR("configuration"),
            GGL_STR("iotDataEndpoint")
        ),
        &resp
    );

    if (ret != GGL_ERR_OK) {
        GGL_LOGW("Failed to get dataplane endpoint from config.");
        return ret;
    }

    return ggl_byte_vec_append(endpoint, resp);
}

GglError get_data_port(GglByteVec *port) {
    static uint8_t resp_mem[128] = { 0 };
    GglBuffer resp = GGL_BUF(resp_mem);

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.Nucleus-Lite"),
            GGL_STR("configuration"),
            GGL_STR("greengrassDataPlanePort")
        ),
        &resp
    );

    if (ret != GGL_ERR_OK) {
        GGL_LOGW("Failed to get dataplane port from config.");
        return ret;
    }

    return ggl_byte_vec_append(port, resp);
}

GglError get_region(GglByteVec *region) {
    static uint8_t resp_mem[128] = { 0 };
    GglBuffer resp = GGL_BUF(resp_mem);

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.Nucleus-Lite"),
            GGL_STR("configuration"),
            GGL_STR("awsRegion")
        ),
        &resp
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW("Failed to get region from config.");
        return ret;
    }

    ggl_byte_vec_chain_append(&ret, region, resp);
    return ret;
}

GglError get_thing_name(char **thing_name) {
    static uint8_t resp_mem[128] = { 0 };
    GglBuffer resp = GGL_BUF(resp_mem);

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("thingName")), &resp
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW("Failed to get thing name from config.");
        return ret;
    }

    *thing_name = (char *) resp.data;
    return GGL_ERR_OK;
}

GglError get_root_ca_path(char **root_ca_path) {
    static uint8_t resp_mem[128] = { 0 };
    GglBuffer resp = GGL_BUF(resp_mem);

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("rootCaPath")), &resp
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW("Failed to get rootCaPath from config.");
        return ret;
    }

    *root_ca_path = (char *) resp.data;
    return GGL_ERR_OK;
}

GglError get_tes_cred_url(char **tes_cred_url) {
    static uint8_t resp_mem[128] = { 0 };
    GglBuffer resp = GGL_BUF(resp_mem);

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.Nucleus-Lite"),
            GGL_STR("configuration"),
            GGL_STR("tesCredUrl")
        ),
        &resp
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW("Failed to get tesCredUrl from config.");
        return ret;
    }

    *tes_cred_url = (char *) resp.data;
    return GGL_ERR_OK;
}

GglError get_posix_user(char **posix_user) {
    static uint8_t resp_mem[128] = { 0 };
    GglBuffer resp = GGL_BUF(resp_mem);

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.Nucleus-Lite"),
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

    *posix_user = (char *) resp.data;
    return GGL_ERR_OK;
}

GglError get_private_key_path(GglByteVec *pkey_path) {
    uint8_t resp_mem[128] = { 0 };
    GglBuffer resp = GGL_BUF(resp_mem);

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("privateKeyPath")), &resp
    );

    if (ret != GGL_ERR_OK) {
        GGL_LOGW("Failed to get private key path from config.");
        return ret;
    }

    ggl_byte_vec_chain_append(&ret, pkey_path, resp);
    return ret;
}

GglError get_cert_path(GglByteVec *cert_path) {
    static uint8_t resp_mem[128] = { 0 };
    GglBuffer resp = GGL_BUF(resp_mem);

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("certificateFilePath")), &resp
    );

    if (ret != GGL_ERR_OK) {
        GGL_LOGW("Failed to get certificate path from config.");
        return ret;
    }

    ggl_byte_vec_chain_append(&ret, cert_path, resp);
    return ret;
}

GglError get_rootca_path(GglByteVec *rootca_path) {
    static uint8_t resp_mem[128] = { 0 };
    GglBuffer resp = GGL_BUF(resp_mem);

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("rootCaPath")), &resp
    );

    if (ret != GGL_ERR_OK) {
        GGL_LOGW("Failed to get rootca path from config.");
        return ret;
    }

    ggl_byte_vec_chain_append(&ret, rootca_path, resp);
    return ret;
}
