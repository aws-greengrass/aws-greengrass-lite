// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_FLEETPROV_PKI_OPS_H
#define GGL_FLEETPROV_PKI_OPS_H

#include <ggl/error.h>

GglError ggl_pki_generate_keypair(
    int private_key_fd, int public_key_fd, int csr_fd
);

#endif
