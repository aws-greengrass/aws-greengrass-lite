// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_DEPLOYMENT_MODEL_H
#define GGDEPLOYMENTD_DEPLOYMENT_MODEL_H

#include "ggl/object.h"
#include <ggl/buffer.h>

#define MAX_COMP_NAME_BUF_SIZE 10000

typedef enum {
    GGL_DEPLOYMENT_QUEUED,
    GGL_DEPLOYMENT_IN_PROGRESS,
} GglDeploymentState;

typedef enum {
    LOCAL_DEPLOYMENT,
    THING_GROUP_DEPLOYMENT,
} GglDeploymentType;

typedef struct {
    GglBuffer deployment_id;
    GglBuffer recipe_directory_path;
    GglBuffer artifacts_directory_path;
    GglBuffer configuration_arn;
    GglBuffer thing_group;
    GglDeploymentState state;
    // Map of component names to map of component information, in cloud
    // deployment doc format
    GglMap components;
    GglDeploymentType type;
} GglDeployment;

#endif
