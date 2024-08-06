// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GENERATE_CERTIFICATE_H
#define GENERATE_CERTIFICATE_H

#include <ggl/error.h>
#include <openssl/types.h>
#include <openssl/x509.h>

void generate_keys(EVP_PKEY **pkey);
void generate_csr(EVP_PKEY *pkey, X509_REQ **req);
void generate_key_files(EVP_PKEY *pkey, X509_REQ *req);

#endif