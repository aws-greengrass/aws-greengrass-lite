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
#include <ggl/json_decode.h>
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
#define MAX_CSR_COMMON_NAME_LEN 256

static const char DEFAULT_CSR_NAME[] = "aws-greengrass-nucleus-lite";

static GglError read_config_str(
    const char *config_path,
    uint8_t *mem_buffer,
    size_t buffer_size,
    char **output
) {
    GglArena alloc = ggl_arena_init(ggl_buffer_substr(
        (GglBuffer) { .data = mem_buffer, .len = buffer_size },
        0,
        buffer_size - 1
    ));
    GglBuffer result;
    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.fleet_provisioning"),
            GGL_STR("configuration"),
            ggl_buffer_from_null_term((char *) config_path)
        ),
        &alloc,
        &result
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    *output = (char *) result.data;
    return GGL_ERR_OK;
}

GglError ggl_load_template_params(
    FleetProvArgs *args, GglArena *alloc, GglMap *template_params
) {
    GglObject result = { 0 };
    GglError ret;

    if (args->template_params_json != NULL) {
        GglBuffer json_buf
            = ggl_buffer_from_null_term(args->template_params_json);
        ret = ggl_json_decode_destructive(json_buf, alloc, &result);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to parse templateParams JSON");
            return ret;
        }
    } else {
        ret = ggl_gg_config_read(
            GGL_BUF_LIST(
                GGL_STR("services"),
                GGL_STR("aws.greengrass.fleet_provisioning"),
                GGL_STR("configuration"),
                GGL_STR("templateParams")
            ),
            alloc,
            &result
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to read "
                     "services/aws.greengrass.fleet_provisioning/configuration/"
                     "templateParams");
            return ret;
        }
    }

    if (ggl_obj_type(result) != GGL_TYPE_MAP) {
        GGL_LOGE("templateParams must be a map");
        return GGL_ERR_INVALID;
    }

    *template_params = ggl_obj_into_map(result);
    return GGL_ERR_OK;
}

static GglError load_csr_common_name(FleetProvArgs *args) {
    if (args->csr_common_name != NULL) {
        return GGL_ERR_OK;
    }

    static uint8_t csr_common_name_mem[MAX_CSR_COMMON_NAME_LEN + 1] = { 0 };
    GglError ret = read_config_str(
        "csrCommonName",
        csr_common_name_mem,
        sizeof(csr_common_name_mem),
        &args->csr_common_name
    );

    if (ret == GGL_ERR_NOENTRY) {
        args->csr_common_name = (char *) DEFAULT_CSR_NAME;
        return GGL_ERR_OK;
    }

    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to read "
                 "services/aws.greengrass.fleet_provisioning/configuration/"
                 "csrCommonName");
    }

    return ret;
}

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
    static uint8_t claim_cert_mem[PATH_MAX] = { 0 };
    static uint8_t claim_key_mem[PATH_MAX] = { 0 };
    static uint8_t root_ca_path_mem[PATH_MAX] = { 0 };
    static uint8_t template_name_mem[MAX_TEMPLATE_LEN + 1] = { 0 };
    static uint8_t endpoint_mem[MAX_ENDPOINT_LENGTH + 1] = { 0 };

    GglError ret;

    if (args->claim_cert == NULL) {
        ret = read_config_str(
            "claimCertPath",
            claim_cert_mem,
            sizeof(claim_cert_mem),
            &args->claim_cert
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to read "
                     "services/aws.greengrass.fleet_provisioning/configuration/"
                     "claimCertPath");
            return ret;
        }
    }

    if (args->claim_key == NULL) {
        ret = read_config_str(
            "claimKeyPath",
            claim_key_mem,
            sizeof(claim_key_mem),
            &args->claim_key
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to read "
                     "services/aws.greengrass.fleet_provisioning/configuration/"
                     "claimKeyPath");
            return ret;
        }
    }

    if (args->root_ca_path == NULL) {
        ret = read_config_str(
            "rootCaPath",
            root_ca_path_mem,
            sizeof(root_ca_path_mem),
            &args->root_ca_path
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to read "
                     "services/aws.greengrass.fleet_provisioning/configuration/"
                     "rootCaPath");
            return ret;
        }
    }

    if (args->template_name == NULL) {
        ret = read_config_str(
            "templateName",
            template_name_mem,
            sizeof(template_name_mem),
            &args->template_name
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to read "
                     "services/aws.greengrass.fleet_provisioning/configuration/"
                     "templateName");
            return ret;
        }
    }

    if (args->endpoint == NULL) {
        ret = read_config_str(
            "iotDataEndpoint",
            endpoint_mem,
            sizeof(endpoint_mem),
            &args->endpoint
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGW("Failed to read "
                     "services/aws.greengrass.fleet_provisioning/configuration/"
                     "iotDataEndpoint");
            return ret;
        }
    }

    return load_csr_common_name(args);
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
