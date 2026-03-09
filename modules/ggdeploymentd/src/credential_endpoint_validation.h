// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_CREDENTIAL_ENDPOINT_VALIDATION_H
#define GGDEPLOYMENTD_CREDENTIAL_ENDPOINT_VALIDATION_H

#include <gg/error.h>
#include <gg/types.h>

/// Verify credential endpoint connectivity by spawning a temporary tesd,
/// making a credential request, and cleaning up. Returns GG_ERR_OK on
/// success; no device state is changed on failure.
GgError check_credential_endpoint(
    GgBuffer cred_endpoint, GgBuffer role_alias, const char *bin_path
);

#endif
