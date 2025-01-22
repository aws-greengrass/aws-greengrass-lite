// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "generate_certificate.h"
#include <fcntl.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/log.h>
#include <openssl/asn1.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <stdio.h>

#define KEY_LENGTH 2048

static GglError generate_keys(EVP_PKEY **pkey) {
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    if (!ctx) {
        GGL_LOGE("Error creating context.");
        return GGL_ERR_FAILURE;
    }

    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        GGL_LOGE("Error initializing keygen.");
        EVP_PKEY_CTX_free(ctx);
        return GGL_ERR_FAILURE;
    }

    if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, KEY_LENGTH) <= 0) {
        GGL_LOGE("Error setting RSA keygen bits.");
        EVP_PKEY_CTX_free(ctx);
        return GGL_ERR_FAILURE;
    }

    if (EVP_PKEY_keygen(ctx, pkey) <= 0) {
        GGL_LOGE("Error generating RSA key.");
        EVP_PKEY_CTX_free(ctx);
        return GGL_ERR_FAILURE;
    }

    EVP_PKEY_CTX_free(ctx);
    return GGL_ERR_OK;
}

static GglError generate_csr(EVP_PKEY *pkey, X509_REQ **req) {
    int ret = 0;
    *req = X509_REQ_new();
    if (req == NULL) {
        GGL_LOGE(
            "Failed to create a openssl x509 certificate signing request object"
        );
        return GGL_ERR_FAILURE;
    }

    X509_REQ_set_version(*req, 1);
    if (ret == 0) {
        GGL_LOGE("x509 csr set version request failed");
        return GGL_ERR_FAILURE;
    }

    X509_NAME *name = X509_NAME_new();
    X509_NAME_add_entry_by_txt(
        name, "C", MBSTRING_ASC, (unsigned char *) "US", -1, -1, 0
    );
    X509_NAME_add_entry_by_txt(
        name, "ST", MBSTRING_ASC, (unsigned char *) "Washington", -1, -1, 0
    );
    X509_NAME_add_entry_by_txt(
        name, "L", MBSTRING_ASC, (unsigned char *) "Seattle", -1, -1, 0
    );
    X509_NAME_add_entry_by_txt(
        name, "O", MBSTRING_ASC, (unsigned char *) "Amazon", -1, -1, 0
    );
    X509_NAME_add_entry_by_txt(
        name, "CN", MBSTRING_ASC, (unsigned char *) "amazon.com", -1, -1, 0
    );
    ret = X509_REQ_set_subject_name(*req, name);
    if (ret == 0) {
        GGL_LOGE("x509 csr subject set request failed");
        return GGL_ERR_FAILURE;
    }

    ret = X509_REQ_set_pubkey(*req, pkey);
    if (ret == 0) {
        GGL_LOGE("x509 csr set pubkey failed");
        return GGL_ERR_FAILURE;
    }
    ret = X509_REQ_sign(*req, pkey, EVP_sha256());
    if (ret == 0) {
        GGL_LOGE("x509 csr sign request failed");
        return GGL_ERR_FAILURE;
    }

    X509_NAME_free(name);
    return GGL_ERR_OK;
}

GglError generate_key_files(
    EVP_PKEY *pkey,
    X509_REQ *req,
    GglBuffer private_file_path,
    GglBuffer public_file_path,
    GglBuffer csr_file_path
) {
    OpenSSL_add_all_algorithms();
    int ret_check = OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
    if (ret_check == 0) {
        GGL_LOGE("Failed to initialize openssl");
        return GGL_ERR_FAILURE;
    }

    GglError ret = generate_keys(&pkey);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (pkey == NULL) {
        GGL_LOGE("Failed to Generate Certificate");
        return GGL_ERR_FAILURE;
    }
    // Save private key
    int pkey_fd = -1;
    ret = ggl_file_open(
        private_file_path,
        O_CREAT | O_TRUNC | O_WRONLY,
        S_IRUSR | S_IWUSR,
        &pkey_fd
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    FILE *pkey_file = fdopen(pkey_fd, "wb");
    if (pkey_file == NULL) {
        GGL_LOGE("Failed to open private key file");
        return GGL_ERR_FATAL;
    }
    ret_check
        = PEM_write_PrivateKey(pkey_file, pkey, NULL, NULL, 0, NULL, NULL);
    if (ret_check == 0) {
        GGL_LOGE("Failed to write private key to disk.");
        return GGL_ERR_FAILURE;
    }

    ret_check = fclose(pkey_file);
    if (ret_check != 0) {
        GGL_LOGE("Failed to close private key file descriptor.");
        return GGL_ERR_FAILURE;
    }

    // Save public key
    int pubkey_fd = -1;
    ret = ggl_file_open(
        public_file_path,
        O_CREAT | O_TRUNC | O_WRONLY,
        S_IRUSR | S_IWUSR,
        &pubkey_fd
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    FILE *pubkey_file = fdopen(pubkey_fd, "wb");
    if (pubkey_file == NULL) {
        GGL_LOGE("Failed to open private key file");
        return GGL_ERR_FATAL;
    }

    ret_check = PEM_write_PUBKEY(pubkey_file, pkey);
    if (ret_check == 0) {
        GGL_LOGE("Failed to write public key to disk.");
        return GGL_ERR_FAILURE;
    }
    ret_check = fclose(pubkey_file);
    if (ret_check != 0) {
        GGL_LOGE("Failed to close public key file descriptor.");
        return GGL_ERR_FAILURE;
    }

    ret = generate_csr(pkey, &req);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    // Save CSR
    int csr_fd = -1;
    ret = ggl_file_open(
        csr_file_path, O_CREAT | O_TRUNC | O_WRONLY, S_IRUSR | S_IWUSR, &csr_fd
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    FILE *csr_file = fdopen(csr_fd, "wb");
    if (csr_file == NULL) {
        GGL_LOGE("Failed to open csr file");
        return GGL_ERR_FATAL;
    }

    ret_check = PEM_write_X509_REQ(csr_file, req);
    if (ret_check == 0) {
        GGL_LOGE("Failed to write csr to disk.");
        return GGL_ERR_FAILURE;
    }
    ret_check = fclose(csr_file);
    if (ret_check != 0) {
        GGL_LOGE("Failed to close csr file descriptor.");
        return GGL_ERR_FAILURE;
    }
    return GGL_ERR_OK;
}
