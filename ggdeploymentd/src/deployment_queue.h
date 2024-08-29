// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_QUEUE_H
#define GGDEPLOYMENTD_QUEUE_H

#include "deployment_model.h"
#include <ggl/error.h>
#include <ggl/object.h>

/// Attempts to add a deployment into the queue.
///
/// If the deployment ID does not exist already in the queue, then add the
/// deployment to the end of the queue. If there is an existing deployment in
/// the queue with the same ID, then replace it if the deployment is in a
/// replaceable state. Otherwise, do not add the deployment to the queue and
/// return an error.
GglError ggl_deployment_enqueue(GglMap deployment_doc, GglBuffer *id);

/// Get the deployment queue for the next deployment.
///
/// Blocks until a deployment is available if the queue is empty.
GglError ggl_deployment_dequeue(GglDeployment **deployment);

/// Release a dequeued deployment
void ggl_deployment_release(GglDeployment *deployment);

#endif
