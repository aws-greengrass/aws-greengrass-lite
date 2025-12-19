// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "pki_ops.h"
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/file.h>
#include <gg/log.h>
#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/provider.h>
#include <openssl/store.h>
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

static void cleanup_provider(OSSL_PROVIDER **provider) {
    OSSL_PROVIDER_unload(*provider);
}

GgError ggl_pki_generate_keypair(
    int private_key_fd, int csr_fd, const char *common_name
) {
    EVP_PKEY *priv_key = EVP_PKEY_Q_keygen(NULL, NULL, "EC", "P-256");
    if (!priv_key) {
        GG_LOGE("Failed to generate new private key.");
        return GG_ERR_FAILURE;
    }
    GG_CLEANUP(cleanup_evp_pkey, priv_key);

    BIO *out = BIO_new_fd(private_key_fd, BIO_NOCLOSE);

    int ssl_ret
        = PEM_write_bio_PrivateKey(out, priv_key, NULL, NULL, 0, NULL, NULL);
    BIO_free(out);
    if (ssl_ret == 0) {
        GG_LOGE("Failed to write new private key.");
        return GG_ERR_FAILURE;
    }

    GgError ret = gg_fsync(private_key_fd);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to fsync private key.");
        return GG_ERR_FAILURE;
    }

    X509_REQ *csr = X509_REQ_new();
    if (csr == NULL) {
        GG_LOGE("Failed to allocate x509 CSR memory.");
        return GG_ERR_FAILURE;
    }
    GG_CLEANUP(cleanup_x509_req, csr);

    X509_NAME *cert_name = X509_NAME_new();
    if (cert_name == NULL) {
        GG_LOGE("Failed to allocate x509 CSR subject name memory.");
        return GG_ERR_FAILURE;
    }
    GG_CLEANUP(cleanup_x509_name, cert_name);

    X509_NAME_add_entry_by_txt(
        cert_name,
        "CN",
        MBSTRING_ASC,
        (const unsigned char *) common_name,
        -1,
        -1,
        0
    );

    ssl_ret = X509_REQ_set_subject_name(csr, cert_name);
    if (ssl_ret == 0) {
        GG_LOGE("Failed to set x509 request subject name.");
        return GG_ERR_FAILURE;
    }

    ssl_ret = X509_REQ_set_pubkey(csr, priv_key);
    if (ssl_ret == 0) {
        GG_LOGE("Failed to set x509 request public key.");
        return GG_ERR_FAILURE;
    }

    ssl_ret = X509_REQ_sign(csr, priv_key, EVP_sha256());
    if (ssl_ret == 0) {
        GG_LOGE("Failed to sign x509 request.");
        return GG_ERR_FAILURE;
    }

    out = BIO_new_fd(csr_fd, BIO_NOCLOSE);
    ssl_ret = PEM_write_bio_X509_REQ(out, csr);
    BIO_free(out);
    if (ssl_ret == 0) {
        GG_LOGE("Failed to write CSR.");
        return GG_ERR_FAILURE;
    }

    ret = gg_fsync(csr_fd);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to fsync CSR.");
        return GG_ERR_FAILURE;
    }

    return GG_ERR_OK;
}

static EVP_PKEY *load_tpm_key(const char *handle_path) {
    OSSL_STORE_CTX *store = NULL;
    EVP_PKEY *pkey = NULL;

    store = OSSL_STORE_open(handle_path, NULL, NULL, NULL, NULL);
    if (!store) {
        return NULL;
    }

    while (!OSSL_STORE_eof(store)) {
        OSSL_STORE_INFO *info = OSSL_STORE_load(store);
        if (!info) {
            break;
        }

        if (OSSL_STORE_INFO_get_type(info) == OSSL_STORE_INFO_PKEY) {
            pkey = OSSL_STORE_INFO_get1_PKEY(info);
            OSSL_STORE_INFO_free(info);
            break;
        }
        OSSL_STORE_INFO_free(info);
    }

    OSSL_STORE_close(store);
    return pkey;
}

GgError ggl_tpm_pki_generate_csr(
    int csr_fd, const char *common_name, const char *tpm_handle_path
) {
    int ssl_ret;
    GgError ret;

    // Load TPM provider
    OSSL_PROVIDER *tpm_provider = OSSL_PROVIDER_load(NULL, "tpm2");
    if (!tpm_provider) {
        GG_LOGE("Failed to load TPM2 provider.");
        return GG_ERR_FAILURE;
    }
    GG_CLEANUP(cleanup_provider, tpm_provider);

    // Load TPM-backed private key
    EVP_PKEY *tpm_key = load_tpm_key(tpm_handle_path);
    if (!tpm_key) {
        GG_LOGE("Failed to load TPM private key from %s.", tpm_handle_path);
        return GG_ERR_FAILURE;
    }
    GG_CLEANUP(cleanup_evp_pkey, tpm_key);

    // Create CSR
    X509_REQ *csr = X509_REQ_new();
    if (!csr) {
        GG_LOGE("Failed to allocate X509 CSR.");
        return GG_ERR_FAILURE;
    }
    GG_CLEANUP(cleanup_x509_req, csr);

    // Build subject name
    X509_NAME *cert_name = X509_NAME_new();
    if (cert_name == NULL) {
        GG_LOGE("Failed to allocate x509 CSR subject name memory.");
        return GG_ERR_FAILURE;
    }
    GG_CLEANUP(cleanup_x509_name, cert_name);

    ssl_ret = X509_NAME_add_entry_by_txt(
        cert_name,
        "CN",
        MBSTRING_ASC,
        (const unsigned char *) common_name,
        -1,
        -1,
        0
    );
    if (ssl_ret == 0) {
        GG_LOGE("Failed to add CN to subject.");
        return GG_ERR_FAILURE;
    }

    ssl_ret = X509_REQ_set_subject_name(csr, cert_name);
    if (ssl_ret == 0) {
        GG_LOGE("Failed to set CSR subject name.");
        return GG_ERR_FAILURE;
    }

    // Attach public key
    ssl_ret = X509_REQ_set_pubkey(csr, tpm_key);
    if (ssl_ret == 0) {
        GG_LOGE("Failed to set CSR public key.");
        return GG_ERR_FAILURE;
    }

    // Sign CSR using TPM-backed key
    ssl_ret = X509_REQ_sign(csr, tpm_key, EVP_sha256());
    if (ssl_ret == 0) {
        GG_LOGE("Failed to sign CSR with TPM key.");
        return GG_ERR_FAILURE;
    }

    // Write CSR to file descriptor
    BIO *out = BIO_new_fd(csr_fd, BIO_NOCLOSE);
    if (!out) {
        GG_LOGE("Failed to create BIO for CSR output.");
        return GG_ERR_FAILURE;
    }

    ssl_ret = PEM_write_bio_X509_REQ(out, csr);
    BIO_free(out);
    if (ssl_ret == 0) {
        GG_LOGE("Failed to write CSR.");
        return GG_ERR_FAILURE;
    }

    // Ensure CSR is flushed to disk
    ret = gg_fsync(csr_fd);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to fsync CSR.");
        return GG_ERR_FAILURE;
    }

    return GG_ERR_OK;
}
