// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "deployment_model.h"
#include <stdint.h>

#ifndef GGDEPLOYMENTD_QUEUE_H
#define GGDEPLOYMENTD_QUEUE_H

/// Initializes the deployment queue as an empty queue with no content. Should only be called once, and subsequent calls will not do anything.
void ggl_deployment_queue_init(void);

/// Attempts to add a deployment into the queue. If the deployment ID does not exist already in the queue, then add the deployment to the end of the queue. If there is an existing deployment in the queue with the same ID, then replace it if the deployment is in a replaceable state. Otherwise, do not add the deployment to the queue and return false. If the queue is full, wait until there is space to add the deployment to the queue.
/// @param deployment a pointer to a GgdeploymentdDeployment to be copied into the queue
/// @return A boolean indicating if the deployment was added to the queue or not
bool ggl_deployment_queue_offer(GgdeploymentdDeployment* deployment);

/// Poll the deployment queue for the next deployment. Wait until a deployment is available if the queue is empty.
/// @return A GgdeploymentdDeployment struct containing the deployment details
GgdeploymentdDeployment ggl_deployment_queue_poll(void);

/// Returns the current size of the deployment queue.
/// @return A uint8_t for the current deployment queue size
uint8_t ggl_deployment_queue_size(void);

#endif
