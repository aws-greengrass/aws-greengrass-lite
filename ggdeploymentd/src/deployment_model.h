// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_DEPLOYMENT_MODEL_H
#define GGDEPLOYMENTD_DEPLOYMENT_MODEL_H

#include "ggl/object.h"
#include <ggl/buffer.h>

typedef enum {
    GGL_DEPLOYMENT_QUEUED,
    GGL_DEPLOYMENT_IN_PROGRESS,
} GglDeploymentState;

typedef struct {
    GglBuffer deployment_id;
    GglBuffer recipe_directory_path;
    GglBuffer artifacts_directory_path;
    GglBuffer configuration_arn;
    GglBuffer thing_group;
    // {component_name -> component_version}
    GglMap root_component_versions_to_add;
    GglList root_components_to_remove;
    GglMap component_to_configuration;
    GglDeploymentState state;
    GglMap cloud_root_components_to_add;
} GglDeployment;

#endif
