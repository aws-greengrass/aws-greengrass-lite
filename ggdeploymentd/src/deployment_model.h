/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGDEPLOYMENTD_DEPLOYMENTMODEL_H
#define GGDEPLOYMENTD_DEPLOYMENTMODEL_H

#include "args.h"
#include "ggl/error.h"
#include "ggl/object.h"

enum DeploymentStage {
    DEFAULT = 0,
    BOOTSTRAP = 1,
    KERNEL_ACTIVATION = 2,
    KERNEL_ROLLBACK = 3,
    ROLLBACK_BOOTSTRAP = 4
};

enum DeploymentType {
    LOCAL = 0,
    SHADOW = 1,
    IOT_JOBS = 2
};

typedef struct {
    GglBuffer recipe_directory_path;
    GglBuffer artifact_directory_path;
    GglMap root_component_versions_to_add;
    GglList root_components_to_remove;
    GglMap component_to_configuration;
    GglMap component_to_run_with_info;
    GglBuffer group_name;
    GglBuffer deployment_id;
    enum DeploymentStage deployment_stage;
    enum DeploymentType deployment_type;
    bool is_cancelled;
} GgdeploymentdLocalDeployment;

#endif