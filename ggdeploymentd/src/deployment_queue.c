/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "deployment_queue.h"
#include "ggl/log.h"

#ifndef GGDEPLOYMENTD_DEPLOYMENT_QUEUE_SIZE
#define GGDEPLOYMENTD_DEPLOYMENT_QUEUE_SIZE 20
#endif

typedef struct {
    GgdeploymentdLocalDeployment deployments[GGDEPLOYMENTD_DEPLOYMENT_QUEUE_SIZE];
    int front;
    int back;
    int size;
    bool initialized;
} GgdeploymentdDeploymentQueue;

static GgdeploymentdDeploymentQueue deployment_queue;

void deployment_queue_init() {
    if(deployment_queue.initialized) {
        GGL_LOGD(
            "deployment_queue",
            "Deployment queue is already initialized, skipping initialization."
        );
        return;
    }
    deployment_queue.front = 0;
    deployment_queue.back = -1;
    deployment_queue.size = 0;
    deployment_queue.initialized = true;
}

int deployment_queue_size() {
    return deployment_queue.size;
}

int deployment_queue_contains_deployment_id(GglBuffer deployment_id) {
    if(deployment_queue.size == 0) {
        return false;
    }
    int count = deployment_queue.size;
    int position = deployment_queue.front;
    while(count > 0) {
        if(ggl_buffer_eq(deployment_queue.deployments[position].deployment_id, deployment_id)) {
            return position;
        }
        position = (position + 1) % GGDEPLOYMENTD_DEPLOYMENT_QUEUE_SIZE;
        count--;
    }
    return -1;
}

bool should_replace_deployment_in_queue(GgdeploymentdLocalDeployment new_deployment, GgdeploymentdLocalDeployment existing_deployment) {
    // If the enqueued deployment is already in progress (Non DEFAULT state), then it can not be replaced.
    if(existing_deployment.deployment_stage != DEFAULT) {
        return false;
    }

    // If the enqueued deployment is of type SHADOW, then replace it.
    // If the offered deployment is cancelled, then replace the enqueued with the offered one
    if (new_deployment.deployment_type == SHADOW || new_deployment.is_cancelled) {
       return true;
    }

    // If the offered deployment is in non DEFAULT stage, then replace the enqueued with the offered one.
    return new_deployment.deployment_stage != DEFAULT;
}

bool offer(GgdeploymentdLocalDeployment deployment) {
    if(deployment_queue.size == GGDEPLOYMENTD_DEPLOYMENT_QUEUE_SIZE) {
        GGL_LOGD(
            "deployment_queue",
            "Deployment queue is full, cannot add deployment to the queue."
        );
        return false;
    }

    int deployment_id_position = deployment_queue_contains_deployment_id(deployment.deployment_id);
    if(deployment_id_position == -1) {
        deployment_queue.back = (deployment_queue.back + 1) % GGDEPLOYMENTD_DEPLOYMENT_QUEUE_SIZE;
        deployment_queue.deployments[deployment_queue.back] = deployment;
        deployment_queue.size++;
        return true;
    }
    else {
        if(should_replace_deployment_in_queue(deployment, deployment_queue.deployments[deployment_id_position])){
            deployment_queue.deployments[deployment_id_position] = deployment;
            return true;
        }
    }
    return false;
}

GgdeploymentdLocalDeployment deployment_queue_poll() {
    if(deployment_queue.size == 0) {
        GGL_LOGD(
            "deployment_queue",
            "Deployment queue is empty, there is no deployment to poll."
        );
        GgdeploymentdLocalDeployment blank_deployment = {};
        return blank_deployment;
    }
    GgdeploymentdLocalDeployment next_deployment = deployment_queue.deployments[deployment_queue.front];
    deployment_queue.front = (deployment_queue.front + 1) % GGDEPLOYMENTD_DEPLOYMENT_QUEUE_SIZE;
    deployment_queue.size--;
    return next_deployment;
}
