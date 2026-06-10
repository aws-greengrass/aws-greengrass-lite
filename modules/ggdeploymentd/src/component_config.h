// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_COMPONENT_CONFIG_H
#define GGDEPLOYMENTD_COMPONENT_CONFIG_H

#include "deployment_model.h"
#include <gg/error.h>
#include <gg/types.h>
#include <stdbool.h>

GgError apply_configurations(
    GglDeployment *deployment, GgBuffer component_name, GgBuffer operation
);

bool is_component_config_updated(
    GglDeployment *deployment, GgBuffer component_name
);

/// Apply a single component's entry from the deployment's
/// componentToConfiguration map. Writes the merge payload into
/// services.<component_name>.configuration in ggconfigd.
GgError apply_component_to_configuration(
    GgBuffer component_name, GgMap component_to_configuration
);

#endif
