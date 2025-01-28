// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_IOT_JOBS_LISTENER_H
#define GGDEPLOYMENTD_IOT_JOBS_LISTENER_H

#include <ggl/buffer.h>
#include <ggl/error.h>
#include <stdint.h>

void *job_listener_thread(void *ctx);

GglError update_current_jobs_deployment(
    GglBuffer deployment_id, GglBuffer status
);
GglError set_jobs_deployment_for_bootstrap(
    GglBuffer job_id, GglBuffer deployment_id, int64_t version
);

#endif
