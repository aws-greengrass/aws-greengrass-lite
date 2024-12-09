// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_BOOTSTRAP_MANAGER_H
#define GGDEPLOYMENTD_BOOTSTRAP_MANAGER_H

#include "deployment_model.h"
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <stdbool.h>

bool bootstrap_required(GglMap recipe, GglBuffer component_name);
GglError save_deployment_state(
    GglDeployment *deployment, GglMap completed_components
);
GglError retrieve_in_progress_deployment(
    GglDeployment *deployment, GglKVVec *deployed_components
);
GglError delete_saved_deployment_from_config(void);
GglError process_bootstrap_phase(
    GglMap components,
    GglBuffer root_path,
    GglBufVec *bootstrap_comp_name_buf_vec
);

#endif
