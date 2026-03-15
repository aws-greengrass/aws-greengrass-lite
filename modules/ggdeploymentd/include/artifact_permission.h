// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_ARTIFACT_PERMISSION_H
#define GGDEPLOYMENTD_ARTIFACT_PERMISSION_H

#include <gg/types.h>
#include <sys/types.h>

mode_t artifact_permission_to_mode(GgMap permission_map);

#endif
