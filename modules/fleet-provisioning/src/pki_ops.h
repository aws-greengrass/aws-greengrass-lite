// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_FLEETPROV_PKI_OPS_H
#define GGL_FLEETPROV_PKI_OPS_H

#include <gg/error.h>

GgError ggl_pki_generate_keypair(
    int private_key_fd, int csr_fd, const char *common_name
);

GgError ggl_tpm_pki_generate_csr(
    int csr_fd, const char *common_name, const char *tpm_handle_path
);
#endif
