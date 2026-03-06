// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef TOKEN_SERVICE_H
#define TOKEN_SERVICE_H

#include <gg/error.h>
#include <gg/types.h>

GgError initiate_request(
    GgBuffer root_ca,
    GgBuffer cert_path,
    GgBuffer key_path,
    GgBuffer thing_name,
    GgBuffer role_alias,
    GgBuffer cred_endpoint,
    GgBuffer interface_name
);

/// Update the credential URL at runtime. Thread-safe.
/// Re-reads both endpoint and role_alias from ggconfigd under a mutex.
void tes_update_cred_url(void);

#endif
