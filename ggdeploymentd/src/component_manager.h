// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_COMPONENT_MANAGER_H
#define GGDEPLOYMENTD_COMPONENT_MANAGER_H

#include "component_model.h"
#include "deployment_model.h"
#include <ggl/error.h>
#include <ggl/object.h>

ComponentMetadata resolve_component_version(
    GglBuffer component_name, GglMap version_requirements
);

#endif
