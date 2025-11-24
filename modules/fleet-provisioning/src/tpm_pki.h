// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef TPM_PKI_H
#define TPM_PKI_H

#include <ggl/buffer.h>
#include <ggl/error.h>

GglError ggl_tpm_generate_keys(void);
GglError ggl_tpm_generate_csr(GglBuffer csr_file_path);

#endif