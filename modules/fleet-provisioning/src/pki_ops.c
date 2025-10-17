// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "pki_ops.h"
#include <ggl/cleanup.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/log.h>
#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/types.h>
#include <openssl/x509.h>
#include <stddef.h>

static void cleanup_evp_pkey(EVP_PKEY **pkey) {
    EVP_PKEY_free(*pkey);
}

static void cleanup_x509_req(X509_REQ **csr) {
    X509_REQ_free(*csr);
}

static void cleanup_x509_name(X509_NAME **name) {
    X509_NAME_free(*name);
}

GglError ggl_pki_generate_keypair(
    int private_key_fd, int public_key_fd, int csr_fd
) {
    EVP_PKEY *pkey = EVP_PKEY_Q_keygen(NULL, NULL, "EC", "P-256");
    if (!pkey) {
        GGL_LOGE("Failed to generate new private key.");
        return GGL_ERR_FAILURE;
    }
    GGL_CLEANUP(cleanup_evp_pkey, pkey);

    BIO *out = BIO_new_fd(private_key_fd, BIO_NOCLOSE);

    int ssl_ret
        = PEM_write_bio_PrivateKey(out, pkey, NULL, NULL, 0, NULL, NULL);
    BIO_free(out);
    if (ssl_ret == 0) {
        GGL_LOGE("Failed to write new private key.");
        return GGL_ERR_FAILURE;
    }

    GglError ret = ggl_fsync(private_key_fd);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to fsync private key.");
        return GGL_ERR_FAILURE;
    }

    out = BIO_new_fd(public_key_fd, BIO_NOCLOSE);
    ssl_ret = PEM_write_bio_PUBKEY(out, pkey);
    BIO_free(out);
    if (ssl_ret == 0) {
        GGL_LOGE("Failed to write new public key.");
        return GGL_ERR_FAILURE;
    }

    ret = ggl_fsync(public_key_fd);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to fsync public key.");
        return GGL_ERR_FAILURE;
    }

    // TODO: get null-term thing name
    const char *device_id = "aws-greengrass-nucleus-lite";

    X509_REQ *csr = X509_REQ_new();
    if (csr == NULL) {
        GGL_LOGE("Failed to allocate x509 CSR memory.");
        return GGL_ERR_FAILURE;
    }
    GGL_CLEANUP(cleanup_x509_req, csr);

    X509_NAME *cert_name = X509_NAME_new();
    if (cert_name == NULL) {
        GGL_LOGE("Failed to allocate x509 CSR subject name memory.");
        return GGL_ERR_FAILURE;
    }
    GGL_CLEANUP(cleanup_x509_name, cert_name);

    X509_NAME_add_entry_by_txt(
        cert_name,
        "CN",
        MBSTRING_ASC,
        (const unsigned char *) device_id,
        -1,
        -1,
        0
    );

    ssl_ret = X509_REQ_set_subject_name(csr, cert_name);
    if (ssl_ret == 0) {
        GGL_LOGE("Failed to set x509 request subject name.");
        return GGL_ERR_FAILURE;
    }

    ssl_ret = X509_REQ_set_pubkey(csr, pkey);
    if (ssl_ret == 0) {
        GGL_LOGE("Failed to set x509 request public key.");
        return GGL_ERR_FAILURE;
    }

    ssl_ret = X509_REQ_sign(csr, pkey, EVP_sha256());
    if (ssl_ret == 0) {
        GGL_LOGE("Failed to sign x509 request.");
        return GGL_ERR_FAILURE;
    }

    out = BIO_new_fd(csr_fd, BIO_NOCLOSE);
    ssl_ret = PEM_write_bio_X509_REQ(out, csr);
    BIO_free(out);
    if (ssl_ret == 0) {
        GGL_LOGE("Failed to write CSR.");
        return GGL_ERR_FAILURE;
    }

    ret = ggl_fsync(csr_fd);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to fsync CSR.");
        return GGL_ERR_FAILURE;
    }

    return GGL_ERR_OK;
}
