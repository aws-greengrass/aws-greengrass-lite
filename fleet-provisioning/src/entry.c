// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "database_helper.h"
#include "fleet-provisioning.h"
#include "generate_certificate.h"
#include "ggl/exec.h"
#include "provisioner.h"
#include <sys/types.h>
#include <ggl/alloc.h>
#include <ggl/bump_alloc.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <openssl/evp.h>
#include <openssl/types.h>
#include <openssl/x509.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#define MAX_PATH_LENGTH 4096
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

    GGL_LOGD("fleet-provisioning", "PID for new iotcored: %d", *iotcored_pid);

    return ret;
}

static GglError fetch_from_db(FleetProvArgs *args, GglAlloc *the_allocator) {
    static char claim_key_path[MAX_PATH_LENGTH] = { 0 };
    static char claim_cert_path[MAX_PATH_LENGTH] = { 0 };
    static char root_ca_path[MAX_PATH_LENGTH] = { 0 };
    static char data_endpoint[MAX_ENDPOINT_LENGTH] = { 0 };
    static char template_name[MAX_TEMPLATE_LEN] = { 0 };
    static char template_parm[MAX_TEMPLATE_PARAM_LEN] = { 0 };

    if (args->claim_cert_path == NULL) {
        GGL_LOGD(
            "fleet-provisioning",
            "Requesting db for "
            "services/aws.greengrass.fleet_provisioning/configuration/"
            "claimCertPath"
        );
        get_value_from_db(
            GGL_LIST(
                GGL_OBJ_STR("services"),
                GGL_OBJ_STR("aws.greengrass.fleet_provisioning"),
                GGL_OBJ_STR("configuration"),
                GGL_OBJ_STR("claimCertPath")
            ),
            the_allocator,
            claim_cert_path
        );
        if (strlen(claim_cert_path) == 0) {
            return GGL_ERR_FATAL;
        }

        args->claim_cert_path = claim_cert_path;
    }

    if (args->claim_key_path == NULL) {
        GGL_LOGD(
            "fleet-provisioning",
            "Requesting db for "
            "services/aws.greengrass.fleet_provisioning/configuration/"
            "claimKeyPath"
        );

        get_value_from_db(
            GGL_LIST(
                GGL_OBJ_STR("services"),
                GGL_OBJ_STR("aws.greengrass.fleet_provisioning"),
                GGL_OBJ_STR("configuration"),
                GGL_OBJ_STR("claimKeyPath")
            ),
            the_allocator,
            claim_key_path
        );
        if (strlen(claim_key_path) == 0) {
            return GGL_ERR_FATAL;
        }

        args->claim_key_path = claim_key_path;
    }

    if (args->root_ca_path == NULL) {
        GGL_LOGD(
            "fleet-provisioning",
            "Requesting db for "
            "system/rootCaPath/"
        );

        get_value_from_db(
            GGL_LIST(GGL_OBJ_STR("system"), GGL_OBJ_STR("rootCaPath")),
            the_allocator,
            root_ca_path
        );
        if (strlen(root_ca_path) == 0) {
            return GGL_ERR_FATAL;
        }

        args->root_ca_path = root_ca_path;
    }

    if (args->data_endpoint == NULL) {
        GGL_LOGD(
            "fleet-provisioning",
            "Requesting db for "
            "services/aws.greengrass.fleet_provisioning/configuration/"
            "iotDataEndpoint"
        );

        get_value_from_db(
            GGL_LIST(
                GGL_OBJ_STR("services"),
                GGL_OBJ_STR("aws.greengrass.fleet_provisioning"),
                GGL_OBJ_STR("configuration"),
                GGL_OBJ_STR("iotDataEndpoint")
            ),
            the_allocator,
            data_endpoint
        );
        if (strlen(data_endpoint) == 0) {
            return GGL_ERR_FATAL;
        }

        args->data_endpoint = data_endpoint;

        GglError ret_save = save_value_to_db(
            GGL_LIST(
                GGL_OBJ_STR("services"),
                GGL_OBJ_STR("aws.greengrass.Nucleus-Lite"),
                GGL_OBJ_STR("configuration")
            ),
            GGL_OBJ_MAP({ GGL_STR("iotDataEndpoint"),
                          GGL_OBJ((GglBuffer
                          ) { .data = (uint8_t *) args->data_endpoint,
                              .len = strlen(args->data_endpoint) }) })
        );
        if (ret_save != GGL_ERR_OK) {
            return ret_save;
        }
    }

    if (args->template_name == NULL) {
        GGL_LOGD(
            "fleet-provisioning",
            "Requesting db for "
            "services/aws.greengrass.fleet_provisioning/configuration/"
            "templateName"
        );

        get_value_from_db(
            GGL_LIST(
                GGL_OBJ_STR("services"),
                GGL_OBJ_STR("aws.greengrass.fleet_provisioning"),
                GGL_OBJ_STR("configuration"),
                GGL_OBJ_STR("templateName")
            ),
            the_allocator,
            template_name
        );
        if (strlen(template_name) == 0) {
            return GGL_ERR_FATAL;
        }

        args->template_name = template_name;
    }

    if (args->template_parameters == NULL) {
        GGL_LOGD(
            "fleet-provisioning",
            "Requesting db for "
            "services/aws.greengrass.fleet_provisioning/configuration/"
            "templateParams"
        );

        get_value_from_db(
            GGL_LIST(
                GGL_OBJ_STR("services"),
                GGL_OBJ_STR("aws.greengrass.fleet_provisioning"),
                GGL_OBJ_STR("configuration"),
                GGL_OBJ_STR("templateParams")
            ),
            the_allocator,
            template_parm
        );
        if (strlen(template_parm) == 0) {
            return GGL_ERR_FATAL;
        }

        args->template_parameters = template_parm;
    }
    return GGL_ERR_OK;
}

GglError run_fleet_prov(FleetProvArgs *args) {
    static uint8_t big_buffer_for_bump[4096];
    static char root_dir[4096] = { 0 };
    GglBumpAlloc the_allocator
        = ggl_bump_alloc_init(GGL_BUF(big_buffer_for_bump));

    GglError ret = fetch_from_db(args, &the_allocator.alloc);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    EVP_PKEY *pkey = NULL;
    X509_REQ *csr_req = NULL;

    GGL_LOGD("fleet-provisioning", "Requesting db for system/rootpath");
    get_value_from_db(
        GGL_LIST(GGL_OBJ_STR("system"), GGL_OBJ_STR("rootpath")),
        &the_allocator.alloc,
        root_dir
    );

    pid_t iotcored_pid = -1;
    ret = start_iotcored(args, &iotcored_pid);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    static char private_file_path[4096] = { 0 };
    static char public_file_path[4096] = { 0 };
    static char csr_file_path[4096] = { 0 };
    static char cert_file_path[4096] = { 0 };

    strncat(private_file_path, root_dir, strlen(root_dir));
    strncat(public_file_path, root_dir, strlen(root_dir));
    strncat(csr_file_path, root_dir, strlen(root_dir));
    strncat(cert_file_path, root_dir, strlen(root_dir));

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

    ret = save_value_to_db(
        GGL_LIST(GGL_OBJ_STR("system")),
        GGL_OBJ_MAP({ GGL_STR("privateKeyPath"),
                      GGL_OBJ((GglBuffer
                      ) { .data = (uint8_t *) private_file_path,
                          .len = strlen(private_file_path) }) })
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "fleet-provisioning", "Something went wrong. Killing iotcored"
        );
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
        GGL_LOGE("fleet-provisioning", "Failed to read th whole file.");
        return GGL_ERR_FAILURE;
    }

    GGL_LOGD(
        "fleet-provisioning",
        "New String: %.*s.",
        (int) strlen(csr_buf),
        csr_buf
    );

    ret = make_request(csr_buf, cert_file_path, iotcored_pid);

    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "fleet-provisioning", "Something went wrong. Killing iotcored"
        );
        exec_kill_process(iotcored_pid);

        return ret;
    }

    return 0;
}
