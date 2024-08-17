// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "database_helper.h"
#include "fleet-provision.h"
#include "fleet-provisioning.h"
#include "generate_certificate.h"
#include <sys/types.h>
#include <ggl/bump_alloc.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <openssl/pem.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

GglError run_fleet_prov(void) {
    static uint8_t big_buffer_for_bump[4096];
    static char root_dir[4096] = { 0 };
    GglBumpAlloc the_allocator
        = ggl_bump_alloc_init(GGL_BUF(big_buffer_for_bump));

    EVP_PKEY *pkey = NULL;
    X509_REQ *csr_req = NULL;

    get_value_from_db(
        GGL_LIST(GGL_OBJ_STR("system"), GGL_OBJ_STR("rootpath")),
        &the_allocator.alloc,
        root_dir
    );

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

    save_value_to_db(
        GGL_LIST(GGL_OBJ_STR("system")),
        GGL_OBJ_MAP({ GGL_STR("privateKeyPath"),
                      GGL_OBJ_STR(private_file_path) })
    );

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

    GGL_LOGI(
        "fleet-provisioning",
        "New String: %.*s.",
        (int) strlen(csr_buf),
        csr_buf
    );

    make_request(csr_buf, cert_file_path);

    return 0;
}
