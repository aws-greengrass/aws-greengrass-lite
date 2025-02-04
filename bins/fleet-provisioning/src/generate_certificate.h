// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef FLEET_PROV_GENERATE_CERTIFICATE_H
#define FLEET_PROV_GENERATE_CERTIFICATE_H

#include <ggl/buffer.h>
#include <ggl/error.h>
#include <openssl/types.h>
#include <openssl/x509.h>

GglError generate_key_files(
    EVP_PKEY *pkey,
    X509_REQ *req,
    GglBuffer private_file_path,
    GglBuffer public_file_path,
    GglBuffer csr_file_path
);

#endif
