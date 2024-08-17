// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "generate_certificate.h"
#include <ggl/log.h>
#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <string.h>
#include <stdio.h>

#define KEY_LENGTH 2048

static void generate_keys(EVP_PKEY **pkey) {
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    if (!ctx) {
        GGL_LOGE("fleet-provisioning", "Error creating context.");
        return;
    }

    if (EVP_PKEY_keygen_init(ctx) <= 0) {
        GGL_LOGE("fleet-provisioning", "Error initializing keygen.");
        EVP_PKEY_CTX_free(ctx);
        return;
    }

    if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, KEY_LENGTH) <= 0) {
        GGL_LOGE("fleet-provisioning", "Error setting RSA keygen bits.");
        EVP_PKEY_CTX_free(ctx);
        return;
    }

    if (EVP_PKEY_keygen(ctx, pkey) <= 0) {
        GGL_LOGE("fleet-provisioning", "Error generating RSA key.");
        EVP_PKEY_CTX_free(ctx);
        return;
    }

    EVP_PKEY_CTX_free(ctx);
}

static void generate_csr(EVP_PKEY *pkey, X509_REQ **req) {
    *req = X509_REQ_new();
    X509_REQ_set_version(*req, 1);

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
    X509_REQ_set_subject_name(*req, name);

    X509_REQ_set_pubkey(*req, pkey);
    X509_REQ_sign(*req, pkey, EVP_sha256());

    X509_NAME_free(name);
}

void generate_key_files(
    EVP_PKEY *pkey,
    X509_REQ *req,
    char *private_file_path,
    char *public_file_path,
    char *csr_file_path
) {
    OpenSSL_add_all_algorithms();
    OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);

    generate_keys(&pkey);

    if (pkey == NULL) {
        GGL_LOGE("fleet-provisioning", "Failed to Generate Certificate");
        return;
    }
    // Save private key
    FILE *pkey_file = fopen(private_file_path, "wb");
    PEM_write_PrivateKey(pkey_file, pkey, NULL, NULL, 0, NULL, NULL);
    fclose(pkey_file);

    // Save public key
    FILE *pubkey_file = fopen(public_file_path, "wb");
    PEM_write_PUBKEY(pubkey_file, pkey);
    fclose(pubkey_file);

    generate_csr(pkey, &req);

    // Save CSR
    FILE *csr_file = fopen(csr_file_path, "wb");
    PEM_write_X509_REQ(csr_file, req);
    fclose(csr_file);
}
