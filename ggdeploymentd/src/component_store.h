// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_COMPONENT_STORE_H
#define GGDEPLOYMENTD_COMPONENT_STORE_H

#include "component_model.h"
#include <ggl/error.h>
#include <ggl/object.h>

void find_available_component(
    GglBuffer component_name,
    GglBuffer requirement,
    ComponentIdentifier *component
);

#endif
