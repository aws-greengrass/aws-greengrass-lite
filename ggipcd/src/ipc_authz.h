// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_IPC_AUTHZ_H
#define GGL_IPC_AUTHZ_H

#include "ipc_service.h"
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <stdbool.h>

typedef bool GglIpcPolicyResourceMatcher(
    GglBuffer request_resource, GglBuffer policy_resource
);

GglError ggl_ipc_auth(
    const GglIpcOperationInfo *info,
    GglBuffer resource,
    GglIpcPolicyResourceMatcher *matcher
);

GglIpcPolicyResourceMatcher ggl_ipc_default_policy_matcher;

#endif
