// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "deployment_configuration.h"
#include <assert.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <limits.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

DeploymentConfiguration config;

GglError get_data_endpoint(GglByteVec *endpoint) {
    uint8_t resp_mem[128] = { 0 };
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
    uint8_t resp_mem[16] = { 0 };
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
    uint8_t resp_mem[30] = { 0 };
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

    ggl_byte_vec_append(region, resp);
    return ret;
}

GglError get_thing_name(GglBuffer *thing_name) {
    uint8_t resp_mem[128] = { 0 };
    GglBuffer resp = GGL_BUF(resp_mem);

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("thingName")), &resp
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW("Failed to get thing name from config.");
        return ret;
    }

    if (thing_name->len < resp.len) {
        assert(false);
        return GGL_ERR_FAILURE;
    }
    memcpy(thing_name->data, resp.data, resp.len);
    thing_name->len = resp.len;
    return GGL_ERR_OK;
}

GglError get_root_ca_path(GglBuffer *root_ca_path) {
    uint8_t resp_mem[PATH_MAX] = { 0 };
    GglBuffer resp = GGL_BUF(resp_mem);

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("rootCaPath")), &resp
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW("Failed to get rootCaPath from config.");
        return ret;
    }

    if (root_ca_path->len < resp.len) {
        assert(false);
        return GGL_ERR_FAILURE;
    }
    memcpy(root_ca_path->data, resp.data, resp.len);
    root_ca_path->len = resp.len;
    return GGL_ERR_OK;
}

GglError get_tes_cred_url(GglBuffer *tes_cred_url) {
    uint8_t resp_mem[128] = { 0 };
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

    if (tes_cred_url->len < resp.len) {
        assert(false);
        return GGL_ERR_FAILURE;
    }
    memcpy(tes_cred_url->data, resp.data, resp.len);
    tes_cred_url->len = resp.len;
    return GGL_ERR_OK;
}

GglError get_posix_user(GglBuffer *posix_user) {
    uint8_t resp_mem[128] = { 0 };
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

    if (posix_user->len < resp.len) {
        assert(false);
        return GGL_ERR_FAILURE;
    }
    memcpy(posix_user->data, resp.data, resp.len);
    posix_user->len = resp.len;
    return GGL_ERR_OK;
}

GglError get_private_key_path(GglByteVec *pkey_path) {
    uint8_t resp_mem[PATH_MAX] = { 0 };
    GglBuffer resp = GGL_BUF(resp_mem);

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("privateKeyPath")), &resp
    );

    if (ret != GGL_ERR_OK) {
        GGL_LOGW("Failed to get private key path from config.");
        return ret;
    }

    ggl_byte_vec_append(pkey_path, resp);
    return ret;
}

GglError get_cert_path(GglByteVec *cert_path) {
    uint8_t resp_mem[PATH_MAX] = { 0 };
    GglBuffer resp = GGL_BUF(resp_mem);

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("certificateFilePath")), &resp
    );

    if (ret != GGL_ERR_OK) {
        GGL_LOGW("Failed to get certificate path from config.");
        return ret;
    }

    ggl_byte_vec_append(cert_path, resp);
    return ret;
}

GglError get_rootca_path(GglByteVec *rootca_path) {
    uint8_t resp_mem[PATH_MAX] = { 0 };
    GglBuffer resp = GGL_BUF(resp_mem);

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("rootCaPath")), &resp
    );

    if (ret != GGL_ERR_OK) {
        GGL_LOGW("Failed to get rootca path from config.");
        return ret;
    }

    ggl_byte_vec_append(rootca_path, resp);
    return ret;
}
