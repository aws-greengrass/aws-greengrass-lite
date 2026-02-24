// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_BOOTSTRAP_MANAGER_H
#define GGDEPLOYMENTD_BOOTSTRAP_MANAGER_H

#include "deployment_model.h"
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/object.h>
#include <gg/vector.h>
#include <stdbool.h>

/*
  deployment info will be saved to config in the following format:

    services:
      DeploymentService:
        deploymentState:
          components:
            component_name1: version
            component_name2: version
            ...
          bootstrapComponents
          deploymentType: local/IoT Jobs
          deploymentDoc:
          jobsID:
*/

bool component_bootstrap_phase_completed(GgBuffer component_name);

// type can be "bootstrap" or "completed"
// bootstrap type indicates that the component's bootstrap steps have completed
// running completed type indicates that the component completed deployment
GgError save_component_info(
    GgBuffer component_name, GgBuffer component_version, GgBuffer type
);

GgError save_iot_jobs_id(GgBuffer jobs_id);
GgError save_deployment_info(GglDeployment *deployment);
GgError retrieve_in_progress_deployment(
    GglDeployment *deployment, GgBuffer *jobs_id
);
GgError delete_saved_deployment_from_config(void);
GgError process_bootstrap_phase(
    GgMap components,
    GgBuffer root_path,
    GgBufVec *bootstrap_comp_name_buf_vec,
    GglDeployment *deployment
);

#endif
