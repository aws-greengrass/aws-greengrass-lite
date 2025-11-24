// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#include "tpm_pki.h"
#include <ggl/log.h>
#include <openssl/crypto.h>
#include <tss2/tss2_esys.h>
#include <tss2/tss2_mu.h>
#include <tss2/tss2_rc.h>
#include <stdio.h>
#include <stdlib.h>

#define TPM_PERSISTENT_HANDLE 0x81000003

static ESYS_CONTEXT *esys_ctx = NULL;

GglError ggl_tpm_generate_keys(void) {
    TSS2_RC rc;

    rc = Esys_Initialize(&esys_ctx, NULL, NULL);
    if (rc != TSS2_RC_SUCCESS) {
        GGL_LOGE("Failed to initialize ESYS context: 0x%x", rc);
        return GGL_ERR_FAILURE;
    }

    // Check if key already exists
    ESYS_TR persistent_handle = ESYS_TR_NONE;
    rc = Esys_TR_FromTPMPublic(esys_ctx, TPM_PERSISTENT_HANDLE, 
                                ESYS_TR_NONE, ESYS_TR_NONE, ESYS_TR_NONE,
                                &persistent_handle);
    if (rc == TSS2_RC_SUCCESS) {
        GGL_LOGI("TPM key already exists at handle 0x%x", TPM_PERSISTENT_HANDLE);
        Esys_TR_Close(esys_ctx, &persistent_handle);
        Esys_Finalize(&esys_ctx);
        return GGL_ERR_INVALID;
    } else if ((rc & ~TPM2_RC_N_MASK) != TPM2_RC_HANDLE) {
        GGL_LOGE("Failed to query TPM handle: 0x%x", rc);
        Esys_Finalize(&esys_ctx);
        return GGL_ERR_FAILURE;
    }

    // Use default ECC P-256 template for primary key
    TPM2B_PUBLIC in_public_primary = {0};
    in_public_primary.publicArea.type = TPM2_ALG_ECC;
    in_public_primary.publicArea.nameAlg = TPM2_ALG_SHA256;
    in_public_primary.publicArea.objectAttributes =
        TPMA_OBJECT_RESTRICTED |
        TPMA_OBJECT_DECRYPT |
        TPMA_OBJECT_FIXEDTPM |
        TPMA_OBJECT_FIXEDPARENT |
        TPMA_OBJECT_SENSITIVEDATAORIGIN |
        TPMA_OBJECT_USERWITHAUTH;
    in_public_primary.publicArea.parameters.eccDetail.symmetric.algorithm = TPM2_ALG_AES;
    in_public_primary.publicArea.parameters.eccDetail.symmetric.keyBits.aes = 128;
    in_public_primary.publicArea.parameters.eccDetail.symmetric.mode.aes = TPM2_ALG_CFB;
    in_public_primary.publicArea.parameters.eccDetail.scheme.scheme = TPM2_ALG_NULL;
    in_public_primary.publicArea.parameters.eccDetail.curveID = TPM2_ECC_NIST_P256;
    in_public_primary.publicArea.parameters.eccDetail.kdf.scheme = TPM2_ALG_NULL;
    in_public_primary.publicArea.unique.ecc.x.size = 0;
    in_public_primary.publicArea.unique.ecc.y.size = 0;

    TPM2B_SENSITIVE_CREATE in_sensitive = {
        .size = 0,
        .sensitive = {
            .userAuth = {.size = 0},
            .data = {.size = 0}
        }
    };

    TPM2B_DATA outside_info = {.size = 0};
    TPML_PCR_SELECTION creation_pcr = {.count = 0};

    // Create primary key
    ESYS_TR primary_handle = ESYS_TR_NONE;
    rc = Esys_CreatePrimary(esys_ctx, ESYS_TR_RH_OWNER,
                            ESYS_TR_PASSWORD, ESYS_TR_NONE, ESYS_TR_NONE,
                            &in_sensitive, &in_public_primary, &outside_info, &creation_pcr,
                            &primary_handle, NULL, NULL, NULL, NULL);
    if (rc != TSS2_RC_SUCCESS) {
        GGL_LOGE("Failed to create primary key: 0x%x", rc);
        Esys_Finalize(&esys_ctx);
        return GGL_ERR_FAILURE;
    }

    // Use default ECC P-256 template for child key
    TPM2B_PUBLIC in_public_child = {0};
    in_public_child.publicArea.type = TPM2_ALG_ECC;
    in_public_child.publicArea.nameAlg = TPM2_ALG_SHA256;
    in_public_child.publicArea.objectAttributes =
        TPMA_OBJECT_SIGN_ENCRYPT |
        TPMA_OBJECT_FIXEDTPM |
        TPMA_OBJECT_FIXEDPARENT |
        TPMA_OBJECT_SENSITIVEDATAORIGIN |
        TPMA_OBJECT_USERWITHAUTH;
    in_public_child.publicArea.parameters.eccDetail.symmetric.algorithm = TPM2_ALG_NULL;
    in_public_child.publicArea.parameters.eccDetail.scheme.scheme = TPM2_ALG_ECDSA;
    in_public_child.publicArea.parameters.eccDetail.scheme.details.ecdsa.hashAlg = TPM2_ALG_SHA256;
    in_public_child.publicArea.parameters.eccDetail.curveID = TPM2_ECC_NIST_P256;
    in_public_child.publicArea.parameters.eccDetail.kdf.scheme = TPM2_ALG_NULL;
    in_public_child.publicArea.unique.ecc.x.size = 0;
    in_public_child.publicArea.unique.ecc.y.size = 0;

    // Create child ECC key
    TPM2B_PRIVATE *out_private = NULL;
    TPM2B_PUBLIC *out_public = NULL;
    rc = Esys_Create(esys_ctx, primary_handle,
                     ESYS_TR_PASSWORD, ESYS_TR_NONE, ESYS_TR_NONE,
                     &in_sensitive, &in_public_child, &outside_info, &creation_pcr,
                     &out_private, &out_public, NULL, NULL, NULL);
    if (rc != TSS2_RC_SUCCESS) {
        GGL_LOGE("Failed to create child key: 0x%x", rc);
        Esys_FlushContext(esys_ctx, primary_handle);
        Esys_Finalize(&esys_ctx);
        return GGL_ERR_FAILURE;
    }

    // Load child key
    ESYS_TR child_handle = ESYS_TR_NONE;
    rc = Esys_Load(esys_ctx, primary_handle,
                   ESYS_TR_PASSWORD, ESYS_TR_NONE, ESYS_TR_NONE,
                   out_private, out_public, &child_handle);
    if (rc != TSS2_RC_SUCCESS) {
        GGL_LOGE("Failed to load child key: 0x%x", rc);
        Esys_FlushContext(esys_ctx, primary_handle);
        Esys_Finalize(&esys_ctx);
        return GGL_ERR_FAILURE;
    }

    // Make child key persistent
    ESYS_TR persistent_out = ESYS_TR_NONE;
    rc = Esys_EvictControl(esys_ctx, ESYS_TR_RH_OWNER, child_handle,
                           ESYS_TR_PASSWORD, ESYS_TR_NONE, ESYS_TR_NONE,
                           TPM_PERSISTENT_HANDLE, &persistent_out);
    if (rc != TSS2_RC_SUCCESS) {
        GGL_LOGE("Failed to make key persistent: 0x%x", rc);
        Esys_FlushContext(esys_ctx, primary_handle);
        Esys_FlushContext(esys_ctx, child_handle);
        Esys_Finalize(&esys_ctx);
        return GGL_ERR_FAILURE;
    }

    // Cleanup
    GGL_LOGI("TPM key created and made persistent at handle 0x%x", TPM_PERSISTENT_HANDLE);
    Esys_FlushContext(esys_ctx, primary_handle);
    Esys_FlushContext(esys_ctx, child_handle);
    if (persistent_out != ESYS_TR_NONE)
        Esys_TR_Close(esys_ctx, &persistent_out);

    free(out_private);
    free(out_public);
    Esys_Finalize(&esys_ctx);
    return GGL_ERR_OK;
}

GglError ggl_tpm_generate_csr(GglBuffer csr_file_path) {

    // Use OpenSSL command with TPM2 provider
    static char cmd[512];
    snprintf(cmd, sizeof(cmd), 
        "openssl req -new -provider tpm2 -key \"handle:0x%x\" "
        "-out %.*s -subj \"/CN=TPMThing\"",
        TPM_PERSISTENT_HANDLE,
        (int)csr_file_path.len, (char*)csr_file_path.data);
    
    GGL_LOGI("Generating CSR with command: %s", cmd);
    
    int result = system(cmd);
    if (result != 0) {
        GGL_LOGE("Failed to generate CSR using OpenSSL command");
        return GGL_ERR_FAILURE;
    }
    
    GGL_LOGI("CSR generated successfully using TPM key");
    return GGL_ERR_OK;
}