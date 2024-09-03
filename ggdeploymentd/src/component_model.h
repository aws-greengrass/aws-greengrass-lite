// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_COMPONENT_MODEL_H
#define GGDEPLOYMENTD_COMPONENT_MODEL_H

#include "ggl/object.h"

typedef struct {
    GglBuffer name;
    GglBuffer version;
} ComponentIdentifier;

typedef struct {
    ComponentIdentifier componentIdentifier;
    // map of dependency package name to version requirement
    GglMap dependencies;
} ComponentMetadata;

#endif
