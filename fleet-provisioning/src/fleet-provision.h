// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef FLEET_PROVISION_H
#define FLEET_PROVISION_H

#include <ggl/bump_alloc.h>
#include <ggl/error.h>
#include <ggl/object.h>
#include <openssl/types.h>
#include <openssl/x509.h>

int make_request(char *csr_as_string);

#endif
