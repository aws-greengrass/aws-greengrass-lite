// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "config_operations.h"
#include "fleet-provisioning.h"
#include "ggl/exec.h"
#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_TEMPLATE_LEN 128
#define MAX_ENDPOINT_LENGTH 128
#define MAX_TEMPLATE_PARAM_LEN 4096

GglError ggl_update_iot_endpoints(FleetProvArgs *args) {
    GglError ret = ggl_gg_config_write(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.NucleusLite"),
            GGL_STR("configuration"),
            GGL_STR("iotDataEndpoint")
        ),
        ggl_obj_buf(ggl_buffer_from_null_term(args->endpoint)),
        &(int64_t) { 3 }
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    static uint8_t endpoint_mem[2048] = { 0 };
    GglArena alloc = ggl_arena_init(GGL_BUF(endpoint_mem));
    GglBuffer cred_endpoint = GGL_BUF(endpoint_mem);
    ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.fleet_provisioning"),
            GGL_STR("configuration"),
            GGL_STR("iotCredEndpoint")
        ),
        &alloc,
        &cred_endpoint
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = ggl_gg_config_write(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.NucleusLite"),
            GGL_STR("configuration"),
            GGL_STR("iotCredEndpoint")
        ),
        ggl_obj_buf(cred_endpoint),
        &(int64_t) { 3 }
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return GGL_ERR_OK;
}

GglError ggl_has_provisioning_config(GglArena alloc, bool *prov_enabled) {
    GglBuffer cert_path = { 0 };
    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.fleet_provisioning"),
            GGL_STR("configuration"),
            GGL_STR("claimCertPath")
        ),
        &alloc,
        &cert_path
    );
    if (ret == GGL_ERR_NOENTRY) {
        *prov_enabled = false;
        return GGL_ERR_OK;
    }
    if (ret != GGL_ERR_OK) {
        GGL_LOGI("Error checking provisioning configuration.");
        return ret;
    }
    *prov_enabled = (cert_path.len > 0);
    return GGL_ERR_OK;
}

GglError ggl_is_already_provisioned(GglArena alloc, bool *provisioned) {
    GglBuffer cert_path = { 0 };
    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("certificateFilePath")),
        &alloc,
        &cert_path
    );
    if (ret == GGL_ERR_NOENTRY) {
        *provisioned = false;
        return GGL_ERR_OK;
    }
    if (ret != GGL_ERR_OK) {
        GGL_LOGI("Error retreiving provisioning status.");
        return ret;
    }
    *provisioned = (cert_path.len > 0);
    return GGL_ERR_OK;
}

GglError ggl_get_configuration(FleetProvArgs *args) {
    if (args->claim_cert == NULL) {
        static uint8_t claim_cert_mem[PATH_MAX] = { 0 };
        GglArena alloc = ggl_arena_init(ggl_buffer_substr(
            GGL_BUF(claim_cert_mem), 0, sizeof(claim_cert_mem) - 1
        ));
        GglBuffer claim_cert = { 0 };
        GglError ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(
                GGL_STR("services"),
                GGL_STR("aws.greengrass.fleet_provisioning"),
                GGL_STR("configuration"),
                GGL_STR("claimCertPath")
            ),
            &alloc,
            &claim_cert
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to read "
                     "services/aws.greengrass.fleet_provisioning/configuration/"
                     "claimCertPath");
            return ret;
        }

        args->claim_cert = (char *) claim_cert.data;
    }

    if (args->claim_key == NULL) {
        static uint8_t claim_key_mem[PATH_MAX] = { 0 };
        GglArena alloc = ggl_arena_init(ggl_buffer_substr(
            GGL_BUF(claim_key_mem), 0, sizeof(claim_key_mem) - 1
        ));
        GglBuffer claim_key;
        GglError ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(
                GGL_STR("services"),
                GGL_STR("aws.greengrass.fleet_provisioning"),
                GGL_STR("configuration"),
                GGL_STR("claimKeyPath")
            ),
            &alloc,
            &claim_key
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to read "
                     "services/aws.greengrass.fleet_provisioning/configuration/"
                     "claimKeyPath");
            return ret;
        }

        args->claim_key = (char *) claim_key.data;
    }

    if (args->root_ca_path == NULL) {
        static uint8_t root_ca_path_mem[PATH_MAX] = { 0 };
        GglArena alloc = ggl_arena_init(ggl_buffer_substr(
            GGL_BUF(root_ca_path_mem), 0, sizeof(root_ca_path_mem) - 1
        ));
        GglBuffer root_ca_path;
        GglError ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(
                GGL_STR("services"),
                GGL_STR("aws.greengrass.fleet_provisioning"),
                GGL_STR("configuration"),
                GGL_STR("rootCaPath")
            ),
            &alloc,
            &root_ca_path
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to read "
                     "services/aws.greengrass.fleet_provisioning/configuration/"
                     "rootCaPath");
            return ret;
        }

        args->root_ca_path = (char *) root_ca_path.data;
    }

    if (args->template_name == NULL) {
        static uint8_t template_name_mem[MAX_TEMPLATE_LEN + 1] = { 0 };
        GglArena alloc = ggl_arena_init(ggl_buffer_substr(
            GGL_BUF(template_name_mem), 0, sizeof(template_name_mem) - 1
        ));
        GglBuffer template_name;
        GglError ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(
                GGL_STR("services"),
                GGL_STR("aws.greengrass.fleet_provisioning"),
                GGL_STR("configuration"),
                GGL_STR("templateName")
            ),
            &alloc,
            &template_name
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to read "
                     "services/aws.greengrass.fleet_provisioning/configuration/"
                     "templateName");
            return ret;
        }

        args->template_name = (char *) template_name.data;
    }

    if (args->template_params == NULL) {
        static uint8_t template_params_mem[MAX_TEMPLATE_PARAM_LEN + 1] = { 0 };
        GglArena alloc = ggl_arena_init(ggl_buffer_substr(
            GGL_BUF(template_params_mem), 0, sizeof(template_params_mem) - 1
        ));
        GglBuffer template_params;
        GglError ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(
                GGL_STR("services"),
                GGL_STR("aws.greengrass.fleet_provisioning"),
                GGL_STR("configuration"),
                GGL_STR("templateParams")
            ),
            &alloc,
            &template_params
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to read "
                     "services/aws.greengrass.fleet_provisioning/configuration/"
                     "templateParams");
            return ret;
        }

        args->template_params = (char *) template_params.data;
    }

    if (args->endpoint == NULL) {
        static uint8_t endpoint_mem[MAX_ENDPOINT_LENGTH + 1] = { 0 };
        GglArena alloc = ggl_arena_init(ggl_buffer_substr(
            GGL_BUF(endpoint_mem), 0, sizeof(endpoint_mem) - 1
        ));
        GglBuffer endpoint;
        GglError ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(
                GGL_STR("services"),
                GGL_STR("aws.greengrass.fleet_provisioning"),
                GGL_STR("configuration"),
                GGL_STR("iotDataEndpoint")
            ),
            &alloc,
            &endpoint
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGW("Failed to read "
                     "services/aws.greengrass.fleet_provisioning/configuration/"
                     "iotDataEndpoint");
            ret = ggl_gg_config_read_str(
                GGL_BUF_LIST(
                    GGL_STR("services"),
                    GGL_STR("aws.greengrass.NucleusLite"),
                    GGL_STR("configuration"),
                    GGL_STR("iotDataEndpoint")
                ),
                &alloc,
                &endpoint
            );
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to read "
                         "services/aws.greengrass.NucleusLite/configuration/"
                         "iotDataEndpoint");
                return ret;
            }
        }

        args->endpoint = (char *) endpoint.data;
    }
    return GGL_ERR_OK;
}

GglError ggl_update_system_cert_paths(
    GglBuffer output_dir_path, FleetProvArgs *args, GglBuffer thing_name
) {
    static uint8_t path_memory[PATH_MAX] = { 0 };
    GglByteVec path_vec = GGL_BYTE_VEC(path_memory);
    GglError ret;

    // Root CA path
    ret = ggl_byte_vec_append(&path_vec, output_dir_path);
    ggl_byte_vec_chain_append(&ret, &path_vec, GGL_STR("/AmazonRootCA.pem"));
    ggl_byte_vec_chain_push(&ret, &path_vec, '\0');
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    const char *cp_args[]
        = { "cp", args->root_ca_path, (char *) path_vec.buf.data, NULL };
    ret = ggl_exec_command(cp_args);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to copy root CA file");
        return ret;
    }

    ret = ggl_gg_config_write(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("rootCaPath")),
        ggl_obj_buf(ggl_buffer_from_null_term((char *) path_vec.buf.data)),
        &(int64_t) { 3 }
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    // Private key path
    path_vec.buf.len = 0;
    ret = ggl_byte_vec_append(&path_vec, output_dir_path);
    ggl_byte_vec_chain_append(&ret, &path_vec, GGL_STR("/priv_key"));
    ggl_byte_vec_chain_push(&ret, &path_vec, '\0');
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = ggl_gg_config_write(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("privateKeyPath")),
        ggl_obj_buf(ggl_buffer_from_null_term((char *) path_vec.buf.data)),
        &(int64_t) { 3 }
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    // Thing name
    ret = ggl_gg_config_write(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("thingName")),
        ggl_obj_buf(thing_name),
        &(int64_t) { 3 }
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    // Certificate path
    path_vec.buf.len = 0;
    ret = ggl_byte_vec_append(&path_vec, output_dir_path);
    ggl_byte_vec_chain_append(&ret, &path_vec, GGL_STR("/certificate.pem"));
    ggl_byte_vec_chain_push(&ret, &path_vec, '\0');
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = ggl_gg_config_write(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("certificateFilePath")),
        ggl_obj_buf(ggl_buffer_from_null_term((char *) path_vec.buf.data)),
        &(int64_t) { 3 }
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return GGL_ERR_OK;
}
