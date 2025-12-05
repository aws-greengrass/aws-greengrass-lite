// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#include "tpm_pki.h"
#include <gg/log.h>
#include <tss2/tss2_esys.h>
#include <tss2_common.h>
#include <tss2_tpm2_types.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static void cleanup_tpm_resources(
    ESYS_CONTEXT *ctx,
    ESYS_TR primary_handle,
    ESYS_TR child_handle,
    TPM2B_PRIVATE *out_private,
    TPM2B_PUBLIC *out_public
) {
    if (primary_handle != ESYS_TR_NONE) {
        Esys_FlushContext(ctx, primary_handle);
    }
    if (child_handle != ESYS_TR_NONE) {
        Esys_FlushContext(ctx, child_handle);
    }
    Esys_Free(out_private);
    Esys_Free(out_public);
    if (ctx != NULL) {
        Esys_Finalize(&ctx);
    }
}

static TPMI_DH_PERSISTENT find_unused_handle(ESYS_CONTEXT *ctx) {
    TSS2_RC rc;
    TPMI_YES_NO more = TPM2_NO;
    TPMS_CAPABILITY_DATA *capability = NULL;

    // Get used handles
    rc = Esys_GetCapability(
        ctx,
        ESYS_TR_NONE,
        ESYS_TR_NONE,
        ESYS_TR_NONE,
        TPM2_CAP_HANDLES,
        TPM2_PERSISTENT_FIRST,
        TPM2_MAX_CAP_HANDLES,
        &more,
        &capability
    );
    if (rc != TSS2_RC_SUCCESS) {
        return 0;
    }

    TPML_HANDLE *handles = &capability->data.handles;

    // Search the unused handle from the range 0x81000000 - 0x81FFFFFF
    for (TPMI_DH_PERSISTENT h = 0x81000000; h <= 0x81FFFFFF; h++) {
        bool used = false;
        for (size_t i = 0; i < handles->count; i++) {
            if (handles->handle[i] == h) {
                used = true;
                break;
            }
        }

        // Unused handle found
        if (!used) {
            Esys_Free(capability);
            return h;
        }
    }

    Esys_Free(capability);
    return 0;
}

static GgError create_primary_key(
    ESYS_CONTEXT *esys_ctx, ESYS_TR *primary_handle
) {
    TSS2_RC rc;

    TPM2B_SENSITIVE_CREATE in_sensitive
        = { .size = 0,
            .sensitive = { .userAuth = { .size = 0 }, .data = { .size = 0 } } };

    // Use default ECC P-256 template for primary key
    TPM2B_PUBLIC in_public_primary = { 0 };
    in_public_primary.publicArea.type = TPM2_ALG_ECC;
    in_public_primary.publicArea.nameAlg = TPM2_ALG_SHA256;
    in_public_primary.publicArea.objectAttributes = TPMA_OBJECT_RESTRICTED
        | TPMA_OBJECT_DECRYPT | TPMA_OBJECT_FIXEDTPM | TPMA_OBJECT_FIXEDPARENT
        | TPMA_OBJECT_SENSITIVEDATAORIGIN | TPMA_OBJECT_USERWITHAUTH;

    in_public_primary.publicArea.parameters.eccDetail.symmetric.algorithm
        = TPM2_ALG_AES;
    in_public_primary.publicArea.parameters.eccDetail.symmetric.keyBits.aes
        = 128;
    in_public_primary.publicArea.parameters.eccDetail.symmetric.mode.aes
        = TPM2_ALG_CFB;
    in_public_primary.publicArea.parameters.eccDetail.scheme.scheme
        = TPM2_ALG_NULL;
    in_public_primary.publicArea.parameters.eccDetail.curveID
        = TPM2_ECC_NIST_P256;
    in_public_primary.publicArea.parameters.eccDetail.kdf.scheme
        = TPM2_ALG_NULL;

    TPM2B_DATA outside_info = { .size = 0 };
    TPML_PCR_SELECTION creation_pcr = { .count = 0 };

    rc = Esys_CreatePrimary(
        esys_ctx,
        ESYS_TR_RH_OWNER,
        ESYS_TR_PASSWORD,
        ESYS_TR_NONE,
        ESYS_TR_NONE,
        &in_sensitive,
        &in_public_primary,
        &outside_info,
        &creation_pcr,
        primary_handle,
        NULL,
        NULL,
        NULL,
        NULL
    );

    if (rc != TSS2_RC_SUCCESS) {
        GG_LOGE("Failed to create primary key: 0x%x", rc);
        return GG_ERR_FAILURE;
    }

    return GG_ERR_OK;
}

static GgError create_private_key(
    ESYS_CONTEXT *esys_ctx,
    ESYS_TR primary_handle,
    TPM2B_PRIVATE **out_private,
    TPM2B_PUBLIC **out_public,
    ESYS_TR *child_handle
) {
    TSS2_RC rc;

    TPM2B_SENSITIVE_CREATE in_sensitive
        = { .size = 0,
            .sensitive = { .userAuth = { .size = 0 }, .data = { .size = 0 } } };

    // Use default ECC P-256 template for child key
    TPM2B_PUBLIC in_public_child = { 0 };
    in_public_child.publicArea.type = TPM2_ALG_ECC;
    in_public_child.publicArea.nameAlg = TPM2_ALG_SHA256;
    in_public_child.publicArea.objectAttributes = TPMA_OBJECT_SIGN_ENCRYPT
        | TPMA_OBJECT_FIXEDTPM | TPMA_OBJECT_FIXEDPARENT
        | TPMA_OBJECT_SENSITIVEDATAORIGIN | TPMA_OBJECT_USERWITHAUTH;

    in_public_child.publicArea.parameters.eccDetail.symmetric.algorithm
        = TPM2_ALG_NULL;
    in_public_child.publicArea.parameters.eccDetail.scheme.scheme
        = TPM2_ALG_ECDSA;
    in_public_child.publicArea.parameters.eccDetail.scheme.details.ecdsa.hashAlg
        = TPM2_ALG_SHA256;
    in_public_child.publicArea.parameters.eccDetail.curveID
        = TPM2_ECC_NIST_P256;
    in_public_child.publicArea.parameters.eccDetail.kdf.scheme = TPM2_ALG_NULL;

    TPM2B_DATA outside_info = { .size = 0 };
    TPML_PCR_SELECTION creation_pcr = { .count = 0 };

    rc = Esys_Create(
        esys_ctx,
        primary_handle,
        ESYS_TR_PASSWORD,
        ESYS_TR_NONE,
        ESYS_TR_NONE,
        &in_sensitive,
        &in_public_child,
        &outside_info,
        &creation_pcr,
        out_private,
        out_public,
        NULL,
        NULL,
        NULL
    );

    if (rc != TSS2_RC_SUCCESS) {
        GG_LOGE("Failed to create child key: 0x%x", rc);
        return GG_ERR_FAILURE;
    }

    rc = Esys_Load(
        esys_ctx,
        primary_handle,
        ESYS_TR_PASSWORD,
        ESYS_TR_NONE,
        ESYS_TR_NONE,
        *out_private,
        *out_public,
        child_handle
    );

    if (rc != TSS2_RC_SUCCESS) {
        GG_LOGE("Failed to load child key: 0x%x", rc);
        return GG_ERR_FAILURE;
    }

    return GG_ERR_OK;
}

static GgError make_key_persistent(
    ESYS_CONTEXT *esys_ctx, ESYS_TR child_handle, TPMI_DH_PERSISTENT handle
) {
    TSS2_RC rc;
    ESYS_TR persistent_out = ESYS_TR_NONE;

    rc = Esys_EvictControl(
        esys_ctx,
        ESYS_TR_RH_OWNER,
        child_handle,
        ESYS_TR_PASSWORD,
        ESYS_TR_NONE,
        ESYS_TR_NONE,
        handle,
        &persistent_out
    );

    if (rc != TSS2_RC_SUCCESS) {
        GG_LOGE("Failed to make key persistent: 0x%x", rc);
        return GG_ERR_FAILURE;
    }

    return GG_ERR_OK;
}

GgError ggl_tpm_generate_keys(TPMI_DH_PERSISTENT *new_handle) {
    GgError ret;
    TSS2_RC rc;
    ESYS_CONTEXT *esys_ctx = NULL;

    // Initialize ESYS context
    rc = Esys_Initialize(&esys_ctx, NULL, NULL);
    if (rc != TSS2_RC_SUCCESS) {
        GG_LOGE("Failed to initialize ESYS context: 0x%x", rc);
        return GG_ERR_FAILURE;
    }

    // Find an unused persistent handle
    *new_handle = find_unused_handle(esys_ctx);
    if (*new_handle == 0) {
        GG_LOGE("No empty TPM handle available");
        Esys_Finalize(&esys_ctx);
        return GG_ERR_FAILURE;
    }

    ESYS_TR primary_handle = ESYS_TR_NONE;
    ESYS_TR child_handle = ESYS_TR_NONE;
    TPM2B_PRIVATE *out_private = NULL;
    TPM2B_PUBLIC *out_public = NULL;

    // Create primary key
    ret = create_primary_key(esys_ctx, &primary_handle);
    if (ret != GG_ERR_OK) {
        Esys_Finalize(&esys_ctx);
        return GG_ERR_FAILURE;
    }

    // Create and load child key
    ret = create_private_key(
        esys_ctx, primary_handle, &out_private, &out_public, &child_handle
    );
    if (ret != GG_ERR_OK) {
        cleanup_tpm_resources(
            esys_ctx, primary_handle, ESYS_TR_NONE, out_private, out_public
        );
        return GG_ERR_FAILURE;
    }

    // Make key persistent
    ret = make_key_persistent(esys_ctx, child_handle, *new_handle);
    if (ret != GG_ERR_OK) {
        cleanup_tpm_resources(
            esys_ctx, primary_handle, child_handle, out_private, out_public
        );
        return GG_ERR_FAILURE;
    }

    // Cleanup & finish
    cleanup_tpm_resources(
        esys_ctx, primary_handle, child_handle, out_private, out_public
    );

    GG_LOGI("TPM key created and made persistent at handle 0x%x", *new_handle);
    return GG_ERR_OK;
}

GgError ggl_tpm_generate_csr(
    GgBuffer csr_file_path, TPMI_DH_PERSISTENT new_handle
) {
    // Use OpenSSL command with TPM2 provider
    static char cmd[512];
    snprintf(
        cmd,
        sizeof(cmd),
        "openssl req -new -provider tpm2 -key \"handle:0x%x\" "
        "-out %.*s -subj \"/CN=TPMThing\"",
        new_handle,
        (int) csr_file_path.len,
        (char *) csr_file_path.data
    );

    GG_LOGD("Generating CSR with command: %s", cmd);

    int result = system(cmd); // NOLINT(concurrency-mt-unsafe)
    if (result != 0) {
        GG_LOGE("Failed to generate CSR using OpenSSL command");
        return GG_ERR_FAILURE;
    }

    GG_LOGI("CSR generated successfully using TPM key");
    return GG_ERR_OK;
}
