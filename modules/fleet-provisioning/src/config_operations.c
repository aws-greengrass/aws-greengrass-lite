// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "config_operations.h"
#include <fleet-provisioning.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/json_decode.h>
#include <gg/log.h>
#include <gg/object.h>
#include <gg/vector.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/exec.h>
#include <limits.h>
#include <tss2_tpm2_types.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define MAX_TEMPLATE_LEN 128
#define MAX_ENDPOINT_LENGTH 128
#define MAX_TEMPLATE_PARAM_LEN 4096
#define MAX_CSR_COMMON_NAME_LEN 256

static const char DEFAULT_CSR_NAME[] = "aws-greengrass-nucleus-lite";

static GgError read_config_str(
    const char *config_path,
    uint8_t *mem_buffer,
    size_t buffer_size,
    char **output
) {
    GgArena alloc = gg_arena_init(gg_buffer_substr(
        (GgBuffer) { .data = mem_buffer, .len = buffer_size },
        0,
        buffer_size - 1
    ));
    GgBuffer result;
    GgError ret = ggl_gg_config_read_str(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("aws.greengrass.fleet_provisioning"),
            GG_STR("configuration"),
            gg_buffer_from_null_term((char *) config_path)
        ),
        &alloc,
        &result
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }
    *output = (char *) result.data;
    return GG_ERR_OK;
}

GgError ggl_load_template_params(
    FleetProvArgs *args, GgArena *alloc, GgMap *template_params
) {
    GgObject result = { 0 };
    GgError ret;

    if (args->template_params_json != NULL) {
        GgBuffer json_buf
            = gg_buffer_from_null_term(args->template_params_json);
        ret = gg_json_decode_destructive(json_buf, alloc, &result);
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to parse templateParams JSON");
            return ret;
        }
    } else {
        ret = ggl_gg_config_read(
            GG_BUF_LIST(
                GG_STR("services"),
                GG_STR("aws.greengrass.fleet_provisioning"),
                GG_STR("configuration"),
                GG_STR("templateParams")
            ),
            alloc,
            &result
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE(
                "Failed to read services/aws.greengrass.fleet_provisioning/configuration/templateParams"
            );
            return ret;
        }
    }

    if (gg_obj_type(result) != GG_TYPE_MAP) {
        GG_LOGE("templateParams must be a map");
        return GG_ERR_INVALID;
    }

    *template_params = gg_obj_into_map(result);
    return GG_ERR_OK;
}

static GgError load_csr_common_name(FleetProvArgs *args) {
    if (args->csr_common_name != NULL) {
        return GG_ERR_OK;
    }

    static uint8_t csr_common_name_mem[MAX_CSR_COMMON_NAME_LEN + 1] = { 0 };
    GgError ret = read_config_str(
        "csrCommonName",
        csr_common_name_mem,
        sizeof(csr_common_name_mem),
        &args->csr_common_name
    );

    if (ret == GG_ERR_NOENTRY) {
        args->csr_common_name = (char *) DEFAULT_CSR_NAME;
        return GG_ERR_OK;
    }

    if (ret != GG_ERR_OK) {
        GG_LOGE(
            "Failed to read services/aws.greengrass.fleet_provisioning/configuration/csrCommonName"
        );
    }

    return ret;
}

static GgError load_required_config(
    const char *key, uint8_t *mem, size_t mem_size, char **output
) {
    if (*output != NULL) {
        return GG_ERR_OK;
    }
    GgError ret = read_config_str(key, mem, mem_size, output);
    if (ret != GG_ERR_OK) {
        GG_LOGE(
            "Failed to read services/aws.greengrass.fleet_provisioning/configuration/%s",
            key
        );
    }
    return ret;
}

static GgError load_optional_config(
    const char *key,
    uint8_t *mem,
    size_t mem_size,
    char **output,
    const char *default_path
) {
    if (*output != NULL) {
        return GG_ERR_OK;
    }
    GgError ret = read_config_str(key, mem, mem_size, output);
    if (ret == GG_ERR_NOENTRY) {
        GG_LOGI("%s not provided, using default path: %s", key, default_path);
        return GG_ERR_OK;
    }
    if (ret != GG_ERR_OK) {
        GG_LOGE(
            "Failed to read services/aws.greengrass.fleet_provisioning/configuration/%s",
            key
        );
    }
    return ret;
}

GgError ggl_update_iot_endpoints(FleetProvArgs *args) {
    GgError ret = ggl_gg_config_write(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("aws.greengrass.NucleusLite"),
            GG_STR("configuration"),
            GG_STR("iotDataEndpoint")
        ),
        gg_obj_buf(gg_buffer_from_null_term(args->endpoint)),
        &(int64_t) { 3 }
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    static uint8_t endpoint_mem[2048] = { 0 };
    GgArena alloc = gg_arena_init(GG_BUF(endpoint_mem));
    GgBuffer cred_endpoint = GG_BUF(endpoint_mem);
    ret = ggl_gg_config_read_str(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("aws.greengrass.fleet_provisioning"),
            GG_STR("configuration"),
            GG_STR("iotCredEndpoint")
        ),
        &alloc,
        &cred_endpoint
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ret = ggl_gg_config_write(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("aws.greengrass.NucleusLite"),
            GG_STR("configuration"),
            GG_STR("iotCredEndpoint")
        ),
        gg_obj_buf(cred_endpoint),
        &(int64_t) { 3 }
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    return GG_ERR_OK;
}

GgError ggl_has_provisioning_config(GgArena alloc, bool *prov_enabled) {
    GgBuffer cert_path = { 0 };
    GgError ret = ggl_gg_config_read_str(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("aws.greengrass.fleet_provisioning"),
            GG_STR("configuration"),
            GG_STR("claimCertPath")
        ),
        &alloc,
        &cert_path
    );
    if (ret == GG_ERR_NOENTRY) {
        *prov_enabled = false;
        return GG_ERR_OK;
    }
    if (ret != GG_ERR_OK) {
        GG_LOGI("Error checking provisioning configuration.");
        return ret;
    }
    *prov_enabled = (cert_path.len > 0);
    return GG_ERR_OK;
}

GgError ggl_is_already_provisioned(GgArena alloc, bool *provisioned) {
    GgBuffer cert_path = { 0 };
    GgError ret = ggl_gg_config_read_str(
        GG_BUF_LIST(GG_STR("system"), GG_STR("certificateFilePath")),
        &alloc,
        &cert_path
    );
    if (ret == GG_ERR_NOENTRY) {
        *provisioned = false;
        return GG_ERR_OK;
    }
    if (ret != GG_ERR_OK) {
        GG_LOGI("Error retreiving provisioning status.");
        return ret;
    }
    *provisioned = (cert_path.len > 0);
    return GG_ERR_OK;
}

GgError ggl_get_configuration(FleetProvArgs *args) {
    static uint8_t claim_cert_mem[PATH_MAX] = { 0 };
    static uint8_t claim_key_mem[PATH_MAX] = { 0 };
    static uint8_t root_ca_path_mem[PATH_MAX] = { 0 };
    static uint8_t template_name_mem[MAX_TEMPLATE_LEN + 1] = { 0 };
    static uint8_t endpoint_mem[MAX_ENDPOINT_LENGTH + 1] = { 0 };
    static uint8_t csr_path_mem[PATH_MAX] = { 0 };
    static uint8_t cert_path_mem[PATH_MAX] = { 0 };
    static uint8_t key_path_mem[PATH_MAX] = { 0 };
    static uint8_t default_path_mem[PATH_MAX] = { 0 };

    GgError ret;

    ret = load_required_config(
        "claimCertPath",
        claim_cert_mem,
        sizeof(claim_cert_mem),
        &args->claim_cert
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ret = load_required_config(
        "claimKeyPath", claim_key_mem, sizeof(claim_key_mem), &args->claim_key
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ret = load_required_config(
        "rootCaPath",
        root_ca_path_mem,
        sizeof(root_ca_path_mem),
        &args->root_ca_path
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ret = load_required_config(
        "templateName",
        template_name_mem,
        sizeof(template_name_mem),
        &args->template_name
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ret = load_required_config(
        "iotDataEndpoint", endpoint_mem, sizeof(endpoint_mem), &args->endpoint
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ret = load_csr_common_name(args);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    GgByteVec path_vec = GG_BYTE_VEC(default_path_mem);
    ret = gg_byte_vec_append(
        &path_vec, GG_STR("/var/lib/greengrass/credentials")
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    size_t base_len = path_vec.buf.len;
    gg_byte_vec_chain_append(&ret, &path_vec, GG_STR("/cert_req.pem"));
    gg_byte_vec_chain_push(&ret, &path_vec, '\0');

    ret = load_optional_config(
        "csrPath",
        csr_path_mem,
        sizeof(csr_path_mem),
        &args->csr_path,
        (char *) path_vec.buf.data
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    path_vec.buf.len = base_len;
    gg_byte_vec_chain_append(&ret, &path_vec, GG_STR("/certificate.pem"));
    gg_byte_vec_chain_push(&ret, &path_vec, '\0');

    ret = load_optional_config(
        "certPath",
        cert_path_mem,
        sizeof(cert_path_mem),
        &args->cert_path,
        (char *) path_vec.buf.data
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    path_vec.buf.len = base_len;
    gg_byte_vec_chain_append(&ret, &path_vec, GG_STR("/priv_key"));
    gg_byte_vec_chain_push(&ret, &path_vec, '\0');

    ret = load_optional_config(
        "keyPath",
        key_path_mem,
        sizeof(key_path_mem),
        &args->key_path,
        (char *) path_vec.buf.data
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    return GG_ERR_OK;
}

GgError ggl_update_system_config(
    GgBuffer output_dir_path,
    FleetProvArgs *args,
    GgBuffer thing_name,
    TPMI_DH_PERSISTENT persist_handle
) {
    static uint8_t path_memory[PATH_MAX] = { 0 };
    GgByteVec path_vec = GG_BYTE_VEC(path_memory);
    GgError ret;

    // Root CA path
    ret = gg_byte_vec_append(&path_vec, output_dir_path);
    gg_byte_vec_chain_append(&ret, &path_vec, GG_STR("/AmazonRootCA.pem"));
    gg_byte_vec_chain_push(&ret, &path_vec, '\0');
    if (ret != GG_ERR_OK) {
        return ret;
    }

    const char *cp_args[]
        = { "cp", args->root_ca_path, (char *) path_vec.buf.data, NULL };
    ret = ggl_exec_command(cp_args);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to copy root CA file");
        return ret;
    }

    ret = ggl_gg_config_write(
        GG_BUF_LIST(GG_STR("system"), GG_STR("rootCaPath")),
        gg_obj_buf(gg_buffer_from_null_term((char *) path_vec.buf.data)),
        &(int64_t) { 3 }
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    // Private key path
    const char *key_path_str;
    if (args->key_path == NULL) {
        path_vec.buf.len = 0;
        // Set the persistent handle as the private key path if TPM is enabled
        if (args->use_tpm && persist_handle != 0) {
            static char handle_buf[32];
            snprintf(
                handle_buf, sizeof(handle_buf), "handle:0x%x", persist_handle
            );
            ret = gg_byte_vec_append(
                &path_vec, gg_buffer_from_null_term((char *) handle_buf)
            );
        } else {
            ret = gg_byte_vec_append(&path_vec, output_dir_path);
            gg_byte_vec_chain_append(&ret, &path_vec, GG_STR("/priv_key"));
        }
        gg_byte_vec_chain_push(&ret, &path_vec, '\0');
        if (ret != GG_ERR_OK) {
            return ret;
        }
        key_path_str = (char *) path_vec.buf.data;
    } else {
        key_path_str = args->key_path;
    }

    ret = ggl_gg_config_write(
        GG_BUF_LIST(GG_STR("system"), GG_STR("privateKeyPath")),
        gg_obj_buf(gg_buffer_from_null_term((char *) key_path_str)),
        &(int64_t) { 3 }
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    // Thing name
    ret = ggl_gg_config_write(
        GG_BUF_LIST(GG_STR("system"), GG_STR("thingName")),
        gg_obj_buf(thing_name),
        &(int64_t) { 3 }
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    return GG_ERR_OK;
}

GgError ggl_update_system_cert_path(
    GgBuffer output_dir_path, FleetProvArgs *args
) {
    static uint8_t cert_path_memory[PATH_MAX] = { 0 };
    GgByteVec path_vec = GG_BYTE_VEC(cert_path_memory);
    GgError ret;

    const char *cert_path_str;
    if (args->cert_path == NULL) {
        ret = gg_byte_vec_append(&path_vec, output_dir_path);
        gg_byte_vec_chain_append(&ret, &path_vec, GG_STR("/certificate.pem"));
        gg_byte_vec_chain_push(&ret, &path_vec, '\0');
        if (ret != GG_ERR_OK) {
            return ret;
        }
        cert_path_str = (char *) path_vec.buf.data;
    } else {
        cert_path_str = args->cert_path;
    }

    // Update system certificate file path
    ret = ggl_gg_config_write(
        GG_BUF_LIST(GG_STR("system"), GG_STR("certificateFilePath")),
        gg_obj_buf(gg_buffer_from_null_term((char *) cert_path_str)),
        &(int64_t) { 3 }
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    return GG_ERR_OK;
}
