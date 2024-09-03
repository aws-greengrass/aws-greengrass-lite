// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_DEPLOYMENT_MODEL_H
#define GGDEPLOYMENTD_DEPLOYMENT_MODEL_H

#include "ggl/object.h"

typedef enum {
    GGL_DEPLOYMENT_QUEUED,
    GGL_DEPLOYMENT_IN_PROGRESS,
} GglDeploymentState;

typedef struct {
    GglBuffer deployment_id;
    GglBuffer recipe_directory_path;
    GglBuffer artifact_directory_path;
    // {component_name -> component_version}
    GglMap root_component_versions_to_add;
    GglList root_components_to_remove;
    GglMap component_to_configuration;
    GglDeploymentState state;
    GglMap cloud_root_components_to_add;
    // {package_name -> resolved_version}
    // can't have list of structs so cutting out some of the info, assuming each
    // package is a root component for now
    // TODO: refactor to a GglDeploymentDocument struct
    GglMap deployment_package_configuration_map;
} GglDeployment;

#endif
