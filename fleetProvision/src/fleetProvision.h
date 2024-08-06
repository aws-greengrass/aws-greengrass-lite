// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef FLEET_PROVISION_H
#define FLEET_PROVISION_H

#include <ggl/error.h>
#include <ggl/object.h>
#include <openssl/types.h>
#include <openssl/x509.h>

void generate_keys(EVP_PKEY **pkey);
void generate_csr(EVP_PKEY *pkey, X509_REQ **req);

int make_request(char *local_csr);
static GglError subscribe_callback(void *ctx, uint32_t handle, GglObject data);

#endif