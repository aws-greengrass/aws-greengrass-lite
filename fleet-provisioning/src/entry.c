// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "fleet-provisioning.h"
#include "generate_certificate.h"
#include "ggl/exec.h"
#include "provisioner.h"
#include "stdbool.h"
#include <sys/types.h>
#include <fcntl.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <limits.h>
#include <openssl/evp.h>
#include <openssl/types.h>
#include <openssl/x509.h>
#include <stdint.h>
#include <stdio.h>

#define MAX_TEMPLATE_LEN 129
#define MAX_ENDPOINT_LENGTH 129
#define MAX_TEMPLATE_PARAM_LEN 4096
#define MAX_PATH_LEN 4096

GglBuffer ggcredentials_path = GGL_STR("/ggcredentials");

static GglError start_iotcored(FleetProvArgs *args, pid_t *iotcored_pid) {
    char *iotcore_d_args[]
        = { args->iotcored_path,  "-n", "iotcoredfleet",       "-e",
            args->data_endpoint,  "-i", args->template_name,   "-r",
            args->root_ca_path,   "-c", args->claim_cert_path, "-k",
            args->claim_key_path, NULL };

    GglError ret = ggl_exec_command_async(iotcore_d_args, iotcored_pid);

    GGL_LOGD("PID for new iotcored: %d", *iotcored_pid);

    return ret;
}

static GglError fetch_from_db(FleetProvArgs *args) {
    if (args->claim_cert_path == NULL) {
        GGL_LOGT("Requesting db for "
                 "services/aws.greengrass.fleet_provisioning/configuration/"
                 "claimCertPath");
        static uint8_t claim_cert_path_mem[PATH_MAX] = { 0 };
        GglBuffer claim_cert_path = GGL_BUF(claim_cert_path_mem);
        claim_cert_path.len -= 1;
        GglError ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(
                GGL_STR("services"),
                GGL_STR("aws.greengrass.fleet_provisioning"),
                GGL_STR("configuration"),
                GGL_STR("claimCertPath")
            ),
            &claim_cert_path
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        args->claim_cert_path = (char *) claim_cert_path_mem;
    }

    if (args->claim_key_path == NULL) {
        GGL_LOGT("Requesting db for "
                 "services/aws.greengrass.fleet_provisioning/configuration/"
                 "claimKeyPath");
        static uint8_t claim_key_path_mem[PATH_MAX] = { 0 };
        GglBuffer claim_key_path = GGL_BUF(claim_key_path_mem);
        claim_key_path.len -= 1;
        GglError ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(
                GGL_STR("services"),
                GGL_STR("aws.greengrass.fleet_provisioning"),
                GGL_STR("configuration"),
                GGL_STR("claimKeyPath")
            ),
            &claim_key_path
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        args->claim_key_path = (char *) claim_key_path_mem;
    }

    if (args->root_ca_path == NULL) {
        GGL_LOGT("Requesting db for "
                 "system/rootCaPath/");
        static uint8_t root_ca_path_mem[PATH_MAX] = { 0 };
        GglBuffer root_ca_path = GGL_BUF(root_ca_path_mem);
        root_ca_path.len -= 1;
        GglError ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(GGL_STR("system"), GGL_STR("rootCaPath")),
            &root_ca_path
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        args->root_ca_path = (char *) root_ca_path_mem;
    }

    if (args->data_endpoint == NULL) {
        GGL_LOGT("Requesting db for "
                 "services/aws.greengrass.fleet_provisioning/configuration/"
                 "iotDataEndpoint");
        static uint8_t data_endpoint_mem[MAX_ENDPOINT_LENGTH + 1] = { 0 };
        GglBuffer data_endpoint = GGL_BUF(data_endpoint_mem);
        data_endpoint.len -= 1;
        GglError ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(
                GGL_STR("services"),
                GGL_STR("aws.greengrass.fleet_provisioning"),
                GGL_STR("configuration"),
                GGL_STR("iotDataEndpoint")
            ),
            &data_endpoint
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        args->data_endpoint = (char *) data_endpoint_mem;

        ret = ggl_gg_config_write(
            GGL_BUF_LIST(
                GGL_STR("services"),
                GGL_STR("aws.greengrass.NucleusLite"),
                GGL_STR("configuration"),
                GGL_STR("iotDataEndpoint")
            ),
            GGL_OBJ_BUF(data_endpoint),
            &(int64_t) { 3 }
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    if (args->template_name == NULL) {
        GGL_LOGT("Requesting db for "
                 "services/aws.greengrass.fleet_provisioning/configuration/"
                 "templateName");
        static uint8_t template_name_mem[MAX_TEMPLATE_LEN + 1] = { 0 };
        GglBuffer template_name = GGL_BUF(template_name_mem);
        template_name.len -= 1;
        GglError ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(
                GGL_STR("services"),
                GGL_STR("aws.greengrass.fleet_provisioning"),
                GGL_STR("configuration"),
                GGL_STR("templateName")
            ),
            &template_name
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        args->template_name = (char *) template_name_mem;
    }

    if (args->template_parameters == NULL) {
        GGL_LOGT("Requesting db for "
                 "services/aws.greengrass.fleet_provisioning/configuration/"
                 "templateParams");
        static uint8_t template_params_mem[MAX_TEMPLATE_PARAM_LEN + 1] = { 0 };
        GglBuffer template_params = GGL_BUF(template_params_mem);
        template_params.len -= 1;
        GglError ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(
                GGL_STR("services"),
                GGL_STR("aws.greengrass.fleet_provisioning"),
                GGL_STR("configuration"),
                GGL_STR("templateParams")
            ),
            &template_params
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        args->template_parameters = (char *) template_params_mem;
    }
    return GGL_ERR_OK;
}

static GglError update_cred_access(char *user_group) {
    char *args[] = { "chown", "-R", user_group, "/ggcredentials/", NULL };

    GglError ret = ggl_exec_command(args);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to change ownership of certificates");
        return ret;
    }

    char *args_reboot[] = { "systemctl", "reboot", NULL };
    ret = ggl_exec_command(args_reboot);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to reboot the device");
        return ret;
    }

    return GGL_ERR_OK;
}

static GglError update_iot_endpoints(void) {
    static uint8_t endpoint_mem[2048] = { 0 };
    GglBuffer data_endpoint = GGL_BUF(endpoint_mem);
    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.fleet_provisioning"),
            GGL_STR("configuration"),
            GGL_STR("iotDataEndpoint")
        ),
        &data_endpoint
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = ggl_gg_config_write(
        GGL_BUF_LIST(
            GGL_STR("system"),
            GGL_STR("aws.greengrass.NucleusLite"),
            GGL_STR("configuration"),
            GGL_STR("iotDataEndpoint")
        ),
        GGL_OBJ_BUF(data_endpoint),
        &(int64_t) { 3 }
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GglBuffer cred_endpoint = GGL_BUF(endpoint_mem);
    ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("aws.greengrass.fleet_provisioning"),
            GGL_STR("configuration"),
            GGL_STR("iotCredEndpoint")
        ),
        &cred_endpoint
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = ggl_gg_config_write(
        GGL_BUF_LIST(
            GGL_STR("system"),
            GGL_STR("aws.greengrass.NucleusLite"),
            GGL_STR("configuration"),
            GGL_STR("iotCredEndpoint")
        ),
        GGL_OBJ_BUF(cred_endpoint),
        &(int64_t) { 3 }
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return GGL_ERR_OK;
}

GglError run_fleet_prov(FleetProvArgs *args, pid_t *pid) {
    int config_dir;
    GglError ret
        = ggl_dir_open(ggcredentials_path, O_RDONLY, false, &config_dir);
    if (ret != GGL_ERR_OK) {
        GGL_LOGI("Could not open ggcredentials directory.");
        return GGL_ERR_FAILURE;
    }

    ret = fetch_from_db(args);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    EVP_PKEY *pkey = NULL;
    X509_REQ *csr_req = NULL;

    GGL_LOGT("Requesting db for system/rootPath");
    static uint8_t root_dir_mem[4096] = { 0 };
    GglBuffer root_dir = GGL_BUF(root_dir_mem);
    ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("rootPath")), &root_dir
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    pid_t iotcored_pid = -1;
    ret = start_iotcored(args, &iotcored_pid);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    *pid = iotcored_pid;

    static uint8_t private_file_path_mem[MAX_PATH_LEN] = { 0 };
    GglByteVec private_file_path_vec = GGL_BYTE_VEC(private_file_path_mem);
    ret = ggl_byte_vec_append(&private_file_path_vec, ggcredentials_path);
    ggl_byte_vec_chain_append(
        &ret, &private_file_path_vec, GGL_STR("/private_key.pem.key")
    );
    ggl_byte_vec_chain_push(&ret, &private_file_path_vec, '\0');
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Error to append private key path");
        return ret;
    }

    static uint8_t public_file_path_mem[MAX_PATH_LEN] = { 0 };
    GglByteVec public_file_path_vec = GGL_BYTE_VEC(public_file_path_mem);
    ret = ggl_byte_vec_append(&public_file_path_vec, ggcredentials_path);
    ggl_byte_vec_chain_append(
        &ret, &public_file_path_vec, GGL_STR("/public_key.pem.key")
    );
    ggl_byte_vec_chain_push(&ret, &public_file_path_vec, '\0');
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Error to append public key path");
        return ret;
    }

    static uint8_t cert_file_path_mem[MAX_PATH_LEN] = { 0 };
    GglByteVec cert_file_path_vec = GGL_BYTE_VEC(cert_file_path_mem);
    ret = ggl_byte_vec_append(&cert_file_path_vec, ggcredentials_path);
    ggl_byte_vec_chain_append(
        &ret, &cert_file_path_vec, GGL_STR("/certificate.pem.crt")
    );
    ggl_byte_vec_chain_push(&ret, &cert_file_path_vec, '\0');
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Error to append certificate key path");
        return ret;
    }

    static uint8_t csr_file_path_mem[MAX_PATH_LEN] = { 0 };
    GglByteVec csr_file_path_vec = GGL_BYTE_VEC(csr_file_path_mem);
    ret = ggl_byte_vec_append(&csr_file_path_vec, ggcredentials_path);
    ggl_byte_vec_chain_append(&ret, &csr_file_path_vec, GGL_STR("/csr.pem"));
    ggl_byte_vec_chain_push(&ret, &csr_file_path_vec, '\0');
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Error to append csr path");
        return ret;
    }

    generate_key_files(
        pkey,
        csr_req,
        (char *) private_file_path_vec.buf.data,
        (char *) public_file_path_vec.buf.data,
        (char *) csr_file_path_vec.buf.data
    );

    EVP_PKEY_free(pkey);
    X509_REQ_free(csr_req);

    ret = ggl_gg_config_write(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("privateKeyPath")),
        GGL_OBJ_BUF(private_file_path_vec.buf),
        &(int64_t) { 3 }
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    static uint8_t csr_mem[2048] = { 0 };

    // Try to read csr into memory
    int fd = -1;
    ret = ggl_file_open(csr_file_path_vec.buf, O_RDONLY, 0, &fd);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Error opening csr file %d", ret);
        return 1;
    }

    GglBuffer csr_buf = GGL_BUF(csr_mem);
    ret = ggl_file_read(fd, &csr_buf);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to read csr file.");
        return GGL_ERR_FAILURE;
    }
    GGL_LOGD("CSR successfully read..");

    ret = make_request(csr_buf, cert_file_path_vec.buf, iotcored_pid);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = update_iot_endpoints();
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = update_cred_access(args->user_group);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return 0;
}
