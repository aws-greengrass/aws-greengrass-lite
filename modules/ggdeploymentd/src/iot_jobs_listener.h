// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_IOT_JOBS_LISTENER_H
#define GGDEPLOYMENTD_IOT_JOBS_LISTENER_H

#include <gg/error.h>
#include <gg/types.h>

void *job_listener_thread(void *ctx);

GgError update_current_jobs_deployment(GgBuffer deployment_id, GgBuffer status);

/// Publish an IoT Jobs status update for the current deployment via the given
/// iotcored socket.
GgError update_current_jobs_deployment_to(
    GgBuffer deployment_id, GgBuffer status, GgBuffer socket_name
);

GgError set_jobs_deployment_for_bootstrap(
    GgBuffer job_id, GgBuffer deployment_id
);

#endif
