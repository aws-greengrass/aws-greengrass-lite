// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef TOKEN_SERVICE_H
#define TOKEN_SERVICE_H

#include <ggl/error.h>
#include <ggl/object.h>

GglError initiate_request(
    char *root_ca,
    char *cert_path,
    char *key_path,
    char *thing_name,
    char *role_alias,
    char *cert_endpoint
);

#endif
