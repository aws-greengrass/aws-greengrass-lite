// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_COMPONENT_MANAGER_H
#define GGDEPLOYMENTD_COMPONENT_MANAGER_H

#include <ggl/error.h>
#include <ggl/object.h>

GglError resolve_component_version(
    GglBuffer component_name, GglMap version_requirements, GglBuffer *version
);

#endif
