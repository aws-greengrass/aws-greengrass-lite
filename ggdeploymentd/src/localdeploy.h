/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGDEPLOYMENTD_LOCALDEPLOY_H
#define GGDEPLOYMENTD_LOCALDEPLOY_H

#include "args.h"
#include "ggl/error.h"
#include "ggl/object.h"
#include <stdint.h>

typedef struct {
    GglBuffer recipe_directory_path;
    GglBuffer artifact_directory_path;
    GglMap root_component_versions_to_add;
    GglList root_components_to_remove;
    GglMap component_to_configuration;
    GglMap component_to_run_with_info;
    GglBuffer group_name;
    GglBuffer deployment_id;
} GgdeploymentdLocalDeployment;


GglError ggdeploymentd_create_local_deployment(const GgdeploymentdLocalDeployment *local_deployment);

#endif
