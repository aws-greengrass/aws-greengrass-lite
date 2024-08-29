// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "deployment_queue.h"
#include "deployment_model.h"
#include <sys/types.h>
#include <assert.h>
#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <pthread.h>
#include <string.h>
#include <uuid/uuid.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef DEPLOYMENT_QUEUE_SIZE
#define DEPLOYMENT_QUEUE_SIZE 10
#endif

#ifndef DEPLOYMENT_MEM_SIZE
#define DEPLOYMENT_MEM_SIZE 5000
#endif

static GglDeployment deployments[DEPLOYMENT_QUEUE_SIZE];
static uint8_t deployment_mem[DEPLOYMENT_QUEUE_SIZE][DEPLOYMENT_MEM_SIZE];
static size_t queue_index = 0;
static size_t queue_count = 0;

static pthread_mutex_t queue_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t notify_cond = PTHREAD_COND_INITIALIZER;

static bool get_matching_deployment(GglBuffer deployment_id, size_t *index) {
    for (size_t i = 0; i < queue_count; i++) {
        size_t index_i = (queue_index + i) % DEPLOYMENT_QUEUE_SIZE;
        if (ggl_buffer_eq(deployment_id, deployments[index_i].deployment_id)) {
            *index = index_i;
            return true;
        }
    }
    return false;
}

static GglError null_terminate_buffer(GglBuffer *buf, GglAlloc *alloc) {
    if (buf->len == 0) {
        *buf = GGL_STR("");
        return GGL_ERR_OK;
    }

    uint8_t *mem = GGL_ALLOCN(alloc, uint8_t, buf->len + 1);
    if (mem == NULL) {
        GGL_LOGE(
            "deployment-queue", "Failed to allocate memory for copying buffer."
        );
        return GGL_ERR_NOMEM;
    }

    memcpy(mem, buf->data, buf->len);
    mem[buf->len] = '\0';
    buf->data = mem;
    return GGL_ERR_OK;
}

static GglError deep_copy_deployment(
    GglDeployment *deployment, GglAlloc *alloc
) {
    assert(deployment != NULL);

    GglObject obj = GGL_OBJ(deployment->deployment_id);
    GglError ret = ggl_obj_deep_copy(&obj, alloc);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    deployment->deployment_id = obj.buf;

    ret = null_terminate_buffer(&deployment->recipe_directory_path, alloc);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = null_terminate_buffer(&deployment->artifact_directory_path, alloc);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    obj = GGL_OBJ(deployment->root_component_versions_to_add);
    ret = ggl_obj_deep_copy(&obj, alloc);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    deployment->root_component_versions_to_add = obj.map;

    obj = GGL_OBJ(deployment->root_components_to_remove);
    ret = ggl_obj_deep_copy(&obj, alloc);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    deployment->root_components_to_remove = obj.list;

    obj = GGL_OBJ(deployment->component_to_configuration);
    ret = ggl_obj_deep_copy(&obj, alloc);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    deployment->component_to_configuration = obj.map;

    return GGL_ERR_OK;
}

static GglError parse_deployment_obj(GglMap args, GglDeployment *doc) {
    GglObject *val;

    *doc = (GglDeployment) { 0 };

    if (ggl_map_get(args, GGL_STR("recipe_directory_path"), &val)) {
        if (val->type != GGL_TYPE_BUF) {
            GGL_LOGE("ggdeploymentd", "Received invalid argument.");
            return GGL_ERR_INVALID;
        }
        doc->recipe_directory_path = val->buf;
    }

    if (ggl_map_get(args, GGL_STR("artifacts_directory_path"), &val)) {
        if (val->type != GGL_TYPE_BUF) {
            GGL_LOGE("ggdeploymentd", "Received invalid argument.");
            return GGL_ERR_INVALID;
        }
        doc->artifact_directory_path = val->buf;
    }

    if (ggl_map_get(args, GGL_STR("root_component_versions_to_add"), &val)) {
        if (val->type != GGL_TYPE_MAP) {
            GGL_LOGE("ggdeploymentd", "Received invalid argument.");
            return GGL_ERR_INVALID;
        }
        doc->root_component_versions_to_add = val->map;
    }

    if (ggl_map_get(args, GGL_STR("root_component_versions_to_remove"), &val)) {
        if (val->type != GGL_TYPE_LIST) {
            GGL_LOGE("ggdeploymentd", "Received invalid argument.");
            return GGL_ERR_INVALID;
        }
        doc->root_components_to_remove = val->list;
    }

    if (ggl_map_get(args, GGL_STR("component_to_configuration"), &val)) {
        if (val->type != GGL_TYPE_MAP) {
            GGL_LOGE("ggdeploymentd", "Received invalid argument.");
            return GGL_ERR_INVALID;
        }
        doc->component_to_configuration = val->map;
    }

    if (ggl_map_get(args, GGL_STR("deployment_id"), &val)) {
        if (val->type != GGL_TYPE_BUF) {
            GGL_LOGE("ggdeploymentd", "Received invalid argument.");
            return GGL_ERR_INVALID;
        }
        doc->deployment_id = val->buf;
    } else {
        static uint8_t uuid_mem[37];
        uuid_t binuuid;
        uuid_generate_random(binuuid);
        uuid_unparse(binuuid, (char *) uuid_mem);
        doc->deployment_id = (GglBuffer) { .data = uuid_mem, .len = 36 };
    }

    return GGL_ERR_OK;
}

GglError ggl_deployment_enqueue(GglMap deployment_doc, GglBuffer *id) {
    pthread_mutex_lock(&queue_mtx);
    GGL_DEFER(pthread_mutex_unlock, queue_mtx);

    GglDeployment new = { 0 };
    GglError ret = parse_deployment_obj(deployment_doc, &new);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (id != NULL) {
        if (new.deployment_id.len > id->len) {
            GGL_LOGD(
                "deployment_queue",
                "Insufficient memory to return deployment id."
            );
            return GGL_ERR_NOMEM;
        }

        memcpy(id->data, new.deployment_id.data, new.deployment_id.len);
        id->len = new.deployment_id.len;
    }

    new.state = GGL_DEPLOYMENT_QUEUED;

    size_t index;
    bool exists = get_matching_deployment(new.deployment_id, &index);
    if (exists) {
        if (deployments[index].state != GGL_DEPLOYMENT_QUEUED) {
            GGL_LOGI(
                "deployment_queue", "Existing deployment not replaceable."
            );
            return GGL_ERR_FAILURE;
        }
        GGL_LOGI("deployment_queue", "Replacing existing deployment in queue.");
    } else {
        if (queue_count >= DEPLOYMENT_QUEUE_SIZE) {
            return GGL_ERR_BUSY;
        }

        GGL_LOGD("deployment_queue", "Adding a new deployment to the queue.");
        index = (queue_index + queue_count) % DEPLOYMENT_QUEUE_SIZE;
        queue_count += 1;
    }

    GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(deployment_mem[index]));
    ret = deep_copy_deployment(&new, &balloc.alloc);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    deployments[index] = new;

    pthread_cond_signal(&notify_cond);

    return GGL_ERR_OK;
}

GglError ggl_deployment_dequeue(GglDeployment **deployment) {
    pthread_mutex_lock(&queue_mtx);
    GGL_DEFER(pthread_mutex_unlock, queue_mtx);

    while (queue_count == 0) {
        pthread_cond_wait(&notify_cond, &queue_mtx);
    }

    deployments[queue_index].state = GGL_DEPLOYMENT_IN_PROGRESS;
    *deployment = &deployments[queue_index];

    GGL_LOGD("deployment_queue", "Set a deployment to in progress.");

    return GGL_ERR_OK;
}

void ggl_deployment_release(GglDeployment *deployment) {
    assert(deployment == &deployments[queue_index]);

    GGL_LOGD("deployment_queue", "Removing deployment from queue.");

    queue_count -= 1;
    queue_index = (queue_index + 1) % DEPLOYMENT_QUEUE_SIZE;
}
