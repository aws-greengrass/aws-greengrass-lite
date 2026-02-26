// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_COMPONENT_CONFIG_H
#define GGDEPLOYMENTD_COMPONENT_CONFIG_H

#include "deployment_model.h"
#include <gg/error.h>
#include <gg/types.h>

GgError apply_configurations(
    GglDeployment *deployment, GgBuffer component_name, GgBuffer operation
);

#endif
