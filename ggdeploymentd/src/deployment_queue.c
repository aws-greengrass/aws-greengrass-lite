// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "deployment_queue.h"
#include "deployment_model.h"
#include <ggl/buffer.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef GGDEPLOYMENTD_DEPLOYMENT_QUEUE_SIZE
#define GGDEPLOYMENTD_DEPLOYMENT_QUEUE_SIZE 20
#endif

size_t deployment_queue_contains_deployment_id(GglBuffer deployment_id);
bool should_replace_deployment_in_queue(
    GgdeploymentdDeployment new_deployment,
    GgdeploymentdDeployment existing_deployment
);

typedef struct {
    GgdeploymentdDeployment deployments[GGDEPLOYMENTD_DEPLOYMENT_QUEUE_SIZE];
    size_t front;
    size_t back;
    uint8_t size;
    bool initialized;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} GgdeploymentdDeploymentQueue;

static GgdeploymentdDeploymentQueue deployment_queue;

void deployment_queue_init(void) {
    if (deployment_queue.initialized) {
        GGL_LOGD(
            "deployment_queue",
            "Deployment queue is already initialized, skipping initialization."
        );
        return;
    }
    deployment_queue.front = 0;
    deployment_queue.back = SIZE_MAX;
    deployment_queue.size = 0;
    deployment_queue.initialized = true;
    pthread_mutex_init(&deployment_queue.mutex, NULL);
    pthread_cond_init(&deployment_queue.not_empty, NULL);
    pthread_cond_init(&deployment_queue.not_full, NULL);
}

uint8_t deployment_queue_size(void) {
    return deployment_queue.size;
}

size_t deployment_queue_contains_deployment_id(GglBuffer deployment_id) {
    if (deployment_queue.size == 0) {
        return SIZE_MAX;
    }
    uint8_t count = deployment_queue.size;
    size_t position = deployment_queue.front;
    while (count > 0) {
        if (ggl_buffer_eq(
                deployment_queue.deployments[position].deployment_id,
                deployment_id
            )) {
            return position;
        }
        position = (position + 1) % GGDEPLOYMENTD_DEPLOYMENT_QUEUE_SIZE;
        count--;
    }
    return SIZE_MAX;
}

bool should_replace_deployment_in_queue(
    GgdeploymentdDeployment new_deployment,
    GgdeploymentdDeployment existing_deployment
) {
    // If the enqueued deployment is already in progress (Non DEFAULT state),
    // then it can not be replaced.
    if (existing_deployment.deployment_stage != DEFAULT) {
        return false;
    }

    // If the enqueued deployment is of type SHADOW, then replace it.
    // If the offered deployment is cancelled, then replace the enqueued with
    // the offered one
    if (new_deployment.deployment_type == SHADOW
        || new_deployment.is_cancelled) {
        return true;
    }

    // If the offered deployment is in non DEFAULT stage, then replace the
    // enqueued with the offered one.
    return new_deployment.deployment_stage != DEFAULT;
}

bool deployment_queue_offer(GgdeploymentdDeployment deployment) {
    pthread_mutex_lock(&deployment_queue.mutex);
    while (deployment_queue.size == GGDEPLOYMENTD_DEPLOYMENT_QUEUE_SIZE) {
        pthread_cond_wait(&deployment_queue.not_full, &deployment_queue.mutex);
    }

    size_t deployment_id_position
        = deployment_queue_contains_deployment_id(deployment.deployment_id);
    if (deployment_id_position == SIZE_MAX) {
        deployment_queue.back = (deployment_queue.back == SIZE_MAX)
            ? 0
            : (deployment_queue.back + 1) % GGDEPLOYMENTD_DEPLOYMENT_QUEUE_SIZE;
        deployment_queue.deployments[deployment_queue.back] = deployment;
        deployment_queue.size++;
        GGL_LOGI("deployment_queue", "Added a new deployment to the queue.");
        pthread_cond_signal(&deployment_queue.not_empty);
        pthread_mutex_unlock(&deployment_queue.mutex);
        return true;
    }
    if (should_replace_deployment_in_queue(
            deployment, deployment_queue.deployments[deployment_id_position]
        )) {
        deployment_queue.deployments[deployment_id_position] = deployment;
        GGL_LOGI(
            "deployment_queue",
            "Replaced existing deployment in queue with updated deployment."
        );
        pthread_mutex_unlock(&deployment_queue.mutex);
        return true;
    }
    GGL_LOGI(
        "deployment_queue",
        "Did not add the deployment to the queue, as it shares an ID with an "
        "existing deployment that is not in a replaceable state."
    );
    pthread_mutex_unlock(&deployment_queue.mutex);
    return false;
}

GgdeploymentdDeployment deployment_queue_poll(void) {
    pthread_mutex_lock(&deployment_queue.mutex);
    if (deployment_queue.size == 0) {
        pthread_cond_wait(&deployment_queue.not_empty, &deployment_queue.mutex);
    }
    GgdeploymentdDeployment next_deployment
        = deployment_queue.deployments[deployment_queue.front];
    deployment_queue.front
        = (deployment_queue.front + 1) % GGDEPLOYMENTD_DEPLOYMENT_QUEUE_SIZE;
    deployment_queue.size--;
    GGL_LOGI(
        "deployment_queue", "Removed a deployment from the front of the queue."
    );
    pthread_cond_signal(&deployment_queue.not_full);
    pthread_mutex_unlock(&deployment_queue.mutex);
    return next_deployment;
}
