// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef TPM_PKI_H
#define TPM_PKI_H

#include <gg/buffer.h>
#include <gg/error.h>
#include <tss2_tpm2_types.h>

GgError ggl_tpm_generate_keys(TPMI_DH_PERSISTENT *new_handle);
GgError ggl_tpm_generate_csr(
    GgBuffer csr_file_path, TPMI_DH_PERSISTENT new_handle
);

#endif
