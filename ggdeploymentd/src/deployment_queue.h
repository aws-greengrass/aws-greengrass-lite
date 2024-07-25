/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "localdeploy.h"

#ifndef GGDEPLOYMENTD_QUEUE_H
#define GGDEPLOYMENTD_QUEUE_H

/** Buffer length for the deployment queue.
 * Can be configured with `-DGGL_DEPLOYMENT_QUEUE_BUFFER_LEN=<N>`. */
#ifndef GGL_DEPLOYMENT_QUEUE_BUFFER_LEN
#define GGL_DEPLOYMENT_QUEUE_BUFFER_LEN (50 * sizeof(GgdeploymentdLocalDeployment))
#endif

bool deployment_queue_offer(GgdeploymentdLocalDeployment deployment);
GgdeploymentdLocalDeployment deployment_queue_poll();
void deployment_queue_clear();
long deployment_queue_size();