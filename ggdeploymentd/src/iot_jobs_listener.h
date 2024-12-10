// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_IOT_JOBS_LISTENER_H
#define GGDEPLOYMENTD_IOT_JOBS_LISTENER_H

#include <ggl/buffer.h>
#include <ggl/error.h>
#include <stdint.h>

void listen_for_jobs_deployments(void);

GglError update_current_jobs_deployment(
    GglBuffer deployment_id, GglBuffer status
);
GglError update_bootstrap_jobs_deployment(
    GglBuffer deployment_id, GglBuffer status, int64_t version
);

#endif
