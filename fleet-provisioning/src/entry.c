// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "fleet-provisioning.h"
#include "generate_certificate.h"
#include "ggl/exec.h"
#include "provisioner.h"
#include <sys/types.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <limits.h>
#include <openssl/evp.h>
#include <openssl/types.h>
#include <openssl/x509.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#define MAX_TEMPLATE_LEN 129
#define MAX_ENDPOINT_LENGTH 129
#define MAX_TEMPLATE_PARAM_LEN 4096

static GglError start_iotcored(FleetProvArgs *args, pid_t *iotcored_pid) {
    char *iotcore_d_args[] = {
        args->iotcored_path,  "-n", "iotcoredfleet",       "-e",
        args->data_endpoint,  "-i", args->template_name,   "-r",
        args->root_ca_path,   "-c", args->claim_cert_path, "-k",
        args->claim_key_path,
    };

    GglError ret
        = exec_command_without_child_wait(iotcore_d_args, iotcored_pid);

    GGL_LOGD("PID for new iotcored: %d", *iotcored_pid);

    return ret;
}

static GglError fetch_from_db(FleetProvArgs *args) {
    if (args->claim_cert_path == NULL) {
        GGL_LOGD("Requesting db for "
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
        GGL_LOGD("Requesting db for "
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
        GGL_LOGD("Requesting db for "
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
        GGL_LOGD("Requesting db for "
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
                GGL_STR("aws.greengrass.Nucleus-Lite"),
                GGL_STR("configuration"),
                GGL_STR("iotDataEndpoint")
            ),
            GGL_OBJ(data_endpoint),
            0
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    if (args->template_name == NULL) {
        GGL_LOGD("Requesting db for "
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
        GGL_LOGD("Requesting db for "
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

GglError run_fleet_prov(FleetProvArgs *args) {
    GglError ret = fetch_from_db(args);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    EVP_PKEY *pkey = NULL;
    X509_REQ *csr_req = NULL;

    GGL_LOGD("Requesting db for system/rootPath");
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

    static char private_file_path[4096] = { 0 };
    static char public_file_path[4096] = { 0 };
    static char csr_file_path[4096] = { 0 };
    static char cert_file_path[4096] = { 0 };

    strncat(private_file_path, (char *) root_dir.data, root_dir.len);
    strncat(public_file_path, (char *) root_dir.data, root_dir.len);
    strncat(csr_file_path, (char *) root_dir.data, root_dir.len);
    strncat(cert_file_path, (char *) root_dir.data, root_dir.len);

    strncat(
        private_file_path, "/private_key.pem", strlen("/private_key.pem.key")
    );
    strncat(public_file_path, "/public_key.pem", strlen("/public_key.pem"));
    strncat(csr_file_path, "/csr.pem", strlen("/csr.pem"));
    strncat(
        cert_file_path, "/certificate.pem.crt", strlen("/certificate.pem.crt")
    );

    generate_key_files(
        pkey, csr_req, private_file_path, public_file_path, csr_file_path
    );

    EVP_PKEY_free(pkey);
    X509_REQ_free(csr_req);

    ret = ggl_gg_config_write(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("privateKeyPath")),
        GGL_OBJ((GglBuffer) { .data = (uint8_t *) private_file_path,
                              .len = strlen(private_file_path) }),
        0
    );
    if (ret != GGL_ERR_OK) {
        exec_kill_process(iotcored_pid);
        return ret;
    }

    static char csr_buf[2048] = { 0 };
    FILE *fp;
    ulong file_size;

    // Open the file in binary mode
    fp = fopen("./csr.pem", "rb");
    if (fp == NULL) {
        perror("Error opening file");
        return 1;
    }

    // Get the file size
    fseek(fp, 0, SEEK_END);
    file_size = (ulong) ftell(fp);
    fseek(fp, 0, SEEK_SET);

    // Read the file into the buffer
    size_t read_size = fread(csr_buf, 1, file_size, fp);

    // Close the file
    fclose(fp);

    if (read_size != file_size) {
        GGL_LOGE("Failed to read th whole file.");
        return GGL_ERR_FAILURE;
    }

    GGL_LOGD("New String: %.*s.", (int) strlen(csr_buf), csr_buf);

    ret = make_request(csr_buf, cert_file_path, iotcored_pid);

    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Something went wrong. Killing iotcored");
        exec_kill_process(iotcored_pid);

        return ret;
    }

    return 0;
}
