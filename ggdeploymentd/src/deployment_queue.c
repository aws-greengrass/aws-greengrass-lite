// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "deployment_queue.h"
#include "deployment_model.h"
#include <assert.h>
#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef GGDEPLOYMENTD_DEPLOYMENT_QUEUE_SIZE
#define GGDEPLOYMENTD_DEPLOYMENT_QUEUE_SIZE 20
#endif

#ifndef GGDEPLOYMENTD_DEPLOYMENT_MEM_SIZE
#define GGDEPLOYMENTD_DEPLOYMENT_MEM_SIZE 5000
#endif

typedef struct {
    GgdeploymentdDeployment deployments[GGDEPLOYMENTD_DEPLOYMENT_QUEUE_SIZE];
    uint8_t deployment_mem[GGDEPLOYMENTD_DEPLOYMENT_QUEUE_SIZE]
                          [GGDEPLOYMENTD_DEPLOYMENT_MEM_SIZE];
    size_t front;
    size_t back;
    uint8_t size;
    bool initialized;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} GgdeploymentdDeploymentQueue;

static GgdeploymentdDeploymentQueue deployment_queue;
static pthread_mutex_t deployment_queue_mtx = PTHREAD_MUTEX_INITIALIZER;

__attribute__((constructor)) static void init_deployment_queue(void) {
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
    pthread_cond_init(&deployment_queue.not_empty, NULL);
    pthread_cond_init(&deployment_queue.not_full, NULL);
}

uint8_t ggl_deployment_queue_size(void) {
    return deployment_queue.size;
}

static size_t deployment_queue_contains_deployment_id(GglBuffer deployment_id) {
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

static bool should_replace_deployment_in_queue(
    GgdeploymentdDeployment new_deployment,
    GgdeploymentdDeployment existing_deployment
) {
    // If the enqueued deployment is already in progress (Non DEFAULT state),
    // then it can not be replaced.
    if (existing_deployment.deployment_stage != GGDEPLOYMENT_DEFAULT) {
        return false;
    }

    // If the enqueued deployment is of type SHADOW, then replace it.
    // If the offered deployment is cancelled, then replace the enqueued with
    // the offered one
    if (new_deployment.deployment_type == GGDEPLOYMENT_SHADOW
        || new_deployment.is_cancelled) {
        return true;
    }

    // If the offered deployment is in non DEFAULT stage, then replace the
    // enqueued with the offered one.
    return new_deployment.deployment_stage != GGDEPLOYMENT_DEFAULT;
}

static GglError deep_copy_deployment(
    GgdeploymentdDeployment *location, GglAlloc *alloc
) {
    assert(location != NULL);

    GGL_LOGD("deployment-queue", "Beginning deep copy of deployment");

    GglObject obj = GGL_OBJ(location->deployment_id);
    GglError ret = ggl_obj_deep_copy(&obj, alloc);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    location->deployment_id = obj.buf;

    obj = GGL_OBJ(location->error_stack);
    ret = ggl_obj_deep_copy(&obj, alloc);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    location->error_stack = obj.list;

    obj = GGL_OBJ(location->error_types);
    ret = ggl_obj_deep_copy(&obj, alloc);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    location->error_types = obj.list;

    obj = GGL_OBJ(location->deployment_document.recipe_directory_path);
    ret = ggl_obj_deep_copy(&obj, alloc);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    location->deployment_document.recipe_directory_path = obj.buf;

    obj = GGL_OBJ(location->deployment_document.artifact_directory_path);
    ret = ggl_obj_deep_copy(&obj, alloc);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    location->deployment_document.artifact_directory_path = obj.buf;

    obj = GGL_OBJ(location->deployment_document.root_component_versions_to_add);
    ret = ggl_obj_deep_copy(&obj, alloc);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    location->deployment_document.root_component_versions_to_add = obj.map;

    obj = GGL_OBJ(location->deployment_document.root_components_to_remove);
    ret = ggl_obj_deep_copy(&obj, alloc);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    location->deployment_document.root_components_to_remove = obj.list;

    obj = GGL_OBJ(location->deployment_document.component_to_configuration);
    ret = ggl_obj_deep_copy(&obj, alloc);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    location->deployment_document.component_to_configuration = obj.map;

    obj = GGL_OBJ(location->deployment_document.component_to_run_with_info);
    ret = ggl_obj_deep_copy(&obj, alloc);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    location->deployment_document.component_to_run_with_info = obj.map;

    obj = GGL_OBJ(location->deployment_document.group_name);
    ret = ggl_obj_deep_copy(&obj, alloc);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    location->deployment_document.group_name = obj.buf;

    obj = GGL_OBJ(location->deployment_document.deployment_id);
    ret = ggl_obj_deep_copy(&obj, alloc);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    location->deployment_document.deployment_id = obj.buf;

    obj = GGL_OBJ(location->deployment_document.configuration_arn);
    ret = ggl_obj_deep_copy(&obj, alloc);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    location->deployment_document.configuration_arn = obj.buf;

    obj = GGL_OBJ(location->deployment_document.required_capabilities);
    ret = ggl_obj_deep_copy(&obj, alloc);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    location->deployment_document.required_capabilities = obj.list;

    obj = GGL_OBJ(location->deployment_document.on_behalf_of);
    ret = ggl_obj_deep_copy(&obj, alloc);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    location->deployment_document.on_behalf_of = obj.buf;

    obj = GGL_OBJ(location->deployment_document.parent_group_name);
    ret = ggl_obj_deep_copy(&obj, alloc);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    location->deployment_document.parent_group_name = obj.buf;

    obj = GGL_OBJ(location->deployment_document.failure_handling_policy);
    ret = ggl_obj_deep_copy(&obj, alloc);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    location->deployment_document.failure_handling_policy = obj.buf;

    obj = GGL_OBJ(location->deployment_document.component_update_policy.action);
    ret = ggl_obj_deep_copy(&obj, alloc);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    location->deployment_document.component_update_policy.action = obj.buf;

    return GGL_ERR_OK;
}

GglError ggl_deployment_queue_offer(GgdeploymentdDeployment *deployment) {
    pthread_mutex_lock(&deployment_queue_mtx);
    GGL_DEFER(pthread_mutex_unlock, deployment_queue_mtx);

    while (deployment_queue.size == GGDEPLOYMENTD_DEPLOYMENT_QUEUE_SIZE) {
        pthread_cond_wait(&deployment_queue.not_full, &deployment_queue_mtx);
    }

    size_t deployment_id_position
        = deployment_queue_contains_deployment_id(deployment->deployment_id);
    if (deployment_id_position == SIZE_MAX) {
        deployment_queue.back = (deployment_queue.back == SIZE_MAX)
            ? 0
            : (deployment_queue.back + 1) % GGDEPLOYMENTD_DEPLOYMENT_QUEUE_SIZE;
        // TODO: Make a deep copy of the deployment
        deployment_queue.deployments[deployment_queue.back] = *deployment;
        GglBumpAlloc balloc = ggl_bump_alloc_init(
            GGL_BUF(deployment_queue.deployment_mem[deployment_queue.back])
        );
        GglError ret = deep_copy_deployment(
            &(deployment_queue.deployments[deployment_queue.back]),
            &balloc.alloc
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        deployment_queue.size++;
        GGL_LOGI("deployment_queue", "Added a new deployment to the queue.");
        pthread_cond_signal(&deployment_queue.not_empty);
        return GGL_ERR_OK;
    }
    if (should_replace_deployment_in_queue(
            *deployment, deployment_queue.deployments[deployment_id_position]
        )) {
        // TODO: Make a deep copy of the deployment
        deployment_queue.deployments[deployment_id_position] = *deployment;
        GglBumpAlloc balloc = ggl_bump_alloc_init(
            GGL_BUF(deployment_queue.deployment_mem[deployment_id_position])
        );
        GglError ret = deep_copy_deployment(
            &(deployment_queue.deployments[deployment_id_position]),
            &balloc.alloc
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        GGL_LOGI(
            "deployment_queue",
            "Replaced existing deployment in queue with updated deployment."
        );
        return GGL_ERR_OK;
    }
    GGL_LOGI(
        "deployment_queue",
        "Did not add the deployment to the queue, as it shares an ID with an "
        "existing deployment that is not in a replaceable state."
    );
    return GGL_ERR_INVALID;
}

GgdeploymentdDeployment ggl_deployment_queue_poll(void) {
    pthread_mutex_lock(&deployment_queue_mtx);
    while (deployment_queue.size == 0) {
        pthread_cond_wait(&deployment_queue.not_empty, &deployment_queue_mtx);
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
    pthread_mutex_unlock(&deployment_queue_mtx);
    return next_deployment;
}
