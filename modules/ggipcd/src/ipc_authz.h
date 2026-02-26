// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_IPC_AUTHZ_H
#define GGL_IPC_AUTHZ_H

#include "ipc_service.h"
#include <gg/error.h>
#include <gg/types.h>
#include <stdbool.h>

typedef bool GglIpcPolicyResourceMatcher(
    GgBuffer request_resource, GgBuffer policy_resource
);

GgError ggl_ipc_auth(
    const GglIpcOperationInfo *info,
    GgBuffer resource,
    GglIpcPolicyResourceMatcher *matcher
);

GglIpcPolicyResourceMatcher ggl_ipc_default_policy_matcher;

#endif
