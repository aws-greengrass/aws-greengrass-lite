// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_ACCESS_CONTROL_VALIDATION_H
#define GGDEPLOYMENTD_ACCESS_CONTROL_VALIDATION_H

#include "deployment_model.h"
#include <gg/error.h>
#include <gg/types.h>

GgError validate_access_control_policies(GgObject *config_obj);

GgError validate_merge_access_control(
    GglDeployment *deployment, GgBuffer component_name
);

#endif
