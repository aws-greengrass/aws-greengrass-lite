#include "fleet-provision.h"
#include "fleet-provisioning.h"
#include "generate_certificate.h"
#include <ggl/error.h>
#include <ggl/log.h>
#include <openssl/pem.h>
#include <string.h>
#include <sys/types.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

GglError run_fleet_prov(void) {
    EVP_PKEY *pkey = NULL;
    X509_REQ *csr_req = NULL;

    generate_key_files(pkey, csr_req);

    EVP_PKEY_free(pkey);
    X509_REQ_free(csr_req);

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

    make_request(csr_buf);

    return 0;
}
