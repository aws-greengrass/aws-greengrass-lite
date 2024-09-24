// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef TOKEN_SERVICE_H
#define TOKEN_SERVICE_H

#include <ggl/error.h>
#include <ggl/object.h>

GglError initiate_request(
    GglBuffer root_ca,
    GglBuffer cert_path,
    GglBuffer key_path,
    GglBuffer thing_name,
    GglBuffer role_alias,
    GglBuffer cred_endpoint
);

#endif
