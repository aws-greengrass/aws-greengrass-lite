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
#include <ggl/cleanup.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/vector.h>
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

#ifndef MAX_LOCAL_COMPONENTS
#define MAX_LOCAL_COMPONENTS 64
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
        GGL_LOGE("Failed to allocate memory for copying buffer.");
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

    GglObject obj = GGL_OBJ_BUF(deployment->deployment_id);
    GglError ret = ggl_obj_deep_copy(&obj, alloc);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    deployment->deployment_id = obj.buf;

    ret = null_terminate_buffer(&deployment->recipe_directory_path, alloc);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = null_terminate_buffer(&deployment->artifacts_directory_path, alloc);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    obj = GGL_OBJ_MAP(deployment->components);
    ret = ggl_obj_deep_copy(&obj, alloc);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    deployment->components = obj.map;

    obj = GGL_OBJ_BUF(deployment->configuration_arn);
    ret = ggl_obj_deep_copy(&obj, alloc);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    deployment->configuration_arn = obj.buf;

    obj = GGL_OBJ_BUF(deployment->thing_group);
    ret = ggl_obj_deep_copy(&obj, alloc);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    deployment->thing_group = obj.buf;

    return GGL_ERR_OK;
}

static void get_slash_and_colon_locations_from_arn(
    GglObject *arn, size_t *slash_index, size_t *last_colon_index
) {
    assert(*slash_index == 0);
    assert(*last_colon_index == 0);
    for (size_t i = arn->buf.len; i > 0; i--) {
        if (arn->buf.data[i - 1] == ':') {
            if (*last_colon_index == 0) {
                *last_colon_index = i - 1;
            }
        }
        if (arn->buf.data[i - 1] == '/') {
            *slash_index = i - 1;
        }
        if (*slash_index != 0 && *last_colon_index != 0) {
            break;
        }
    }
}

static GglError parse_deployment_obj(
    GglMap args,
    GglDeployment *doc,
    GglDeploymentType type,
    GglAlloc *alloc,
    GglKVVec *local_components_kv_vec
) {
    *doc = (GglDeployment) { 0 };

    GglObject *recipe_directory_path;
    GglObject *artifacts_directory_path;
    GglObject *root_component_versions_to_add;
    GglObject *cloud_components;
    GglObject *deployment_id;
    GglObject *configuration_arn;

    GglError ret = ggl_map_validate(
        args,
        GGL_MAP_SCHEMA(
            { GGL_STR("recipe_directory_path"),
              false,
              GGL_TYPE_BUF,
              &recipe_directory_path },
            { GGL_STR("artifacts_directory_path"),
              false,
              GGL_TYPE_BUF,
              &artifacts_directory_path },
            { GGL_STR("root_component_versions_to_add"),
              false,
              GGL_TYPE_MAP,
              &root_component_versions_to_add },
            { GGL_STR("components"), false, GGL_TYPE_MAP, &cloud_components },
            { GGL_STR("deployment_id"), false, GGL_TYPE_BUF, &deployment_id },
            { GGL_STR("configurationArn"),
              false,
              GGL_TYPE_BUF,
              &configuration_arn },
        )
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Received invalid argument.");
        return GGL_ERR_INVALID;
    }

    if (recipe_directory_path != NULL) {
        doc->recipe_directory_path = recipe_directory_path->buf;
    }

    if (artifacts_directory_path != NULL) {
        doc->artifacts_directory_path = artifacts_directory_path->buf;
    }

    if (deployment_id != NULL) {
        doc->deployment_id = deployment_id->buf;
    } else {
        static uint8_t uuid_mem[37];
        uuid_t binuuid;
        uuid_generate_random(binuuid);
        uuid_unparse(binuuid, (char *) uuid_mem);
        doc->deployment_id = (GglBuffer) { .data = uuid_mem, .len = 36 };
    }

    if (type == THING_GROUP_DEPLOYMENT) {
        if (cloud_components != NULL) {
            doc->components = cloud_components->map;
        } else {
            GGL_LOGW("Deployment is of type thing group deployment but does "
                     "not have component information.");
        }

        if (configuration_arn != NULL) {
            // Assume that the arn has a version at the end, we want to discard
            // the version for the arn.
            size_t last_colon_index = 0;
            size_t slash_index = 0;
            get_slash_and_colon_locations_from_arn(
                configuration_arn, &slash_index, &last_colon_index
            );
            doc->configuration_arn = configuration_arn->buf;
            doc->thing_group = ggl_buffer_substr(
                configuration_arn->buf, slash_index + 1, last_colon_index
            );
        }
    }

    if (type == LOCAL_DEPLOYMENT) {
        doc->thing_group = GGL_STR("LOCAL_DEPLOYMENTS");
        doc->configuration_arn = doc->deployment_id;

        GglObject local_deployment_root_components_read_value;
        ret = ggl_gg_config_read(
            GGL_BUF_LIST(
                GGL_STR("services"),
                GGL_STR("DeploymentService"),
                GGL_STR("thingGroupsToRootComponents"),
                GGL_STR("LOCAL_DEPLOYMENTS")
            ),
            alloc,
            &local_deployment_root_components_read_value
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGI("No info found in config for root components for local "
                     "deployments, assuming no components have been deployed "
                     "locally yet.");
            // If no components existed in past deployments, then there is
            // nothing to remove and the list of components for local deployment
            // is just components to add.
            GGL_MAP_FOREACH(
                component_pair, root_component_versions_to_add->map
            ) {
                if (component_pair->val.type != GGL_TYPE_BUF) {
                    GGL_LOGE("Local deployment component version read "
                             "incorrectly from the deployment doc.");
                    return GGL_ERR_INVALID;
                }

                // TODO: Add configurationUpdate and runWith
                GglKV *new_component_info_mem = GGL_ALLOC(alloc, GglKV);
                if (new_component_info_mem == NULL) {
                    GGL_LOGE("No memory when allocating memory while enqueuing "
                             "local deployment.");
                    return GGL_ERR_NOMEM;
                }
                *new_component_info_mem
                    = (GglKV) { GGL_STR("version"), component_pair->val };
                GglMap new_component_info_map
                    = (GglMap) { .pairs = new_component_info_mem, .len = 1 };

                ret = ggl_kv_vec_push(
                    local_components_kv_vec,
                    (GglKV) { component_pair->key,
                              GGL_OBJ_MAP(new_component_info_map) }
                );
                if (ret != GGL_ERR_OK) {
                    return ret;
                }
            }

            doc->components = local_components_kv_vec->map;
        } else {
            if (local_deployment_root_components_read_value.type
                != GGL_TYPE_MAP) {
                GGL_LOGE("Local deployment component list read incorrectly "
                         "from the config.");
                return GGL_ERR_INVALID;
            }
            // Pre-populate with all local components that already have been
            // deployed
            GGL_MAP_FOREACH(
                old_component_pair,
                local_deployment_root_components_read_value.map
            ) {
                if (old_component_pair->val.type != GGL_TYPE_BUF) {
                    GGL_LOGE("Local deployment component version read "
                             "incorrectly from the config.");
                    return GGL_ERR_INVALID;
                }

                GGL_LOGD(
                    "Found existing local component %.*s as part of local "
                    "deployments group.",
                    (int) old_component_pair->key.len,
                    old_component_pair->key.data
                );

                GglKV *old_component_info_mem = GGL_ALLOC(alloc, GglKV);
                if (old_component_info_mem == NULL) {
                    GGL_LOGE("No memory when allocating memory while enqueuing "
                             "local deployment.");
                    return GGL_ERR_NOMEM;
                }
                *old_component_info_mem
                    = (GglKV) { GGL_STR("version"), old_component_pair->val };
                GglMap old_component_info_map
                    = (GglMap) { .pairs = old_component_info_mem, .len = 1 };

                ret = ggl_kv_vec_push(
                    local_components_kv_vec,
                    (GglKV) { old_component_pair->key,
                              GGL_OBJ_MAP(old_component_info_map) }
                );
                if (ret != GGL_ERR_OK) {
                    return ret;
                }
            }

            // Add the component to add to the existing list of locally deployed
            // components, or update the version if it already exists.
            GGL_MAP_FOREACH(
                component_pair, root_component_versions_to_add->map
            ) {
                if (component_pair->val.type != GGL_TYPE_BUF) {
                    GGL_LOGE("Local deployment component version read "
                             "incorrectly from the deployment doc.");
                    return GGL_ERR_INVALID;
                }

                GglObject *existing_component_data;
                // TODO: Remove component if it is in the removal list.
                if (!ggl_map_get(
                        local_components_kv_vec->map,
                        component_pair->key,
                        &existing_component_data
                    )) {
                    GGL_LOGD(
                        "Locally deployed component not previously deployed, "
                        "adding it to the list of local components."
                    );
                    // TODO: Add configurationUpdate and runWith
                    GglKV *new_component_info_mem = GGL_ALLOC(alloc, GglKV);
                    if (new_component_info_mem == NULL) {
                        GGL_LOGE("No memory when allocating memory while "
                                 "enqueuing local deployment.");
                        return GGL_ERR_NOMEM;
                    }
                    *new_component_info_mem
                        = (GglKV) { GGL_STR("version"), component_pair->val };
                    GglMap new_component_info_map = (GglMap
                    ) { .pairs = new_component_info_mem, .len = 1 };

                    ret = ggl_kv_vec_push(
                        local_components_kv_vec,
                        (GglKV) { component_pair->key,
                                  GGL_OBJ_MAP(new_component_info_map) }
                    );
                    if (ret != GGL_ERR_OK) {
                        return ret;
                    }
                } else {
                    GglKV *new_component_info_mem = GGL_ALLOC(alloc, GglKV);
                    if (new_component_info_mem == NULL) {
                        GGL_LOGE("No memory when allocating memory while "
                                 "enqueuing local deployment.");
                        return GGL_ERR_NOMEM;
                    }
                    *new_component_info_mem
                        = (GglKV) { GGL_STR("version"), component_pair->val };
                    GglMap new_component_info_map = (GglMap
                    ) { .pairs = new_component_info_mem, .len = 1 };
                    *existing_component_data
                        = GGL_OBJ_MAP(new_component_info_map);
                }
            }

            doc->components = local_components_kv_vec->map;
        }
    }

    return GGL_ERR_OK;
}

GglError ggl_deployment_enqueue(
    GglMap deployment_doc, GglByteVec *id, GglDeploymentType type
) {
    GGL_MTX_SCOPE_GUARD(&queue_mtx);

    // We are reading a map that may contain MAX_LOCAL_COMPONENTS names to
    // version mappings. This mem is limited to this function call but we deep
    // copy into static memory later in this function.
    uint8_t local_deployment_shortlived_balloc_buf
        [(1 + 2 * MAX_LOCAL_COMPONENTS) * sizeof(GglObject)];
    GglBumpAlloc shortlived_balloc
        = ggl_bump_alloc_init(GGL_BUF(local_deployment_shortlived_balloc_buf));
    GglDeployment new = { 0 };
    GglKVVec local_components_kv_vec
        = GGL_KV_VEC((GglKV[MAX_LOCAL_COMPONENTS]) { 0 });
    GglError ret = parse_deployment_obj(
        deployment_doc,
        &new,
        type,
        &shortlived_balloc.alloc,
        &local_components_kv_vec
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    new.type = type;

    if (id != NULL) {
        ret = ggl_byte_vec_append(id, new.deployment_id);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("insufficient id length");
            return ret;
        }
    }

    new.state = GGL_DEPLOYMENT_QUEUED;

    size_t index;
    bool exists = get_matching_deployment(new.deployment_id, &index);
    if (exists) {
        if (deployments[index].state != GGL_DEPLOYMENT_QUEUED) {
            GGL_LOGI("Existing deployment not replaceable.");
            return GGL_ERR_FAILURE;
        }
        GGL_LOGI("Replacing existing deployment in queue.");
    } else {
        if (queue_count >= DEPLOYMENT_QUEUE_SIZE) {
            return GGL_ERR_BUSY;
        }

        GGL_LOGD("Adding a new deployment to the queue.");
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
    GGL_MTX_SCOPE_GUARD(&queue_mtx);

    while (queue_count == 0) {
        pthread_cond_wait(&notify_cond, &queue_mtx);
    }

    deployments[queue_index].state = GGL_DEPLOYMENT_IN_PROGRESS;
    *deployment = &deployments[queue_index];

    GGL_LOGD("Set a deployment to in progress.");

    return GGL_ERR_OK;
}

void ggl_deployment_release(GglDeployment *deployment) {
    assert(deployment == &deployments[queue_index]);

    GGL_LOGD("Removing deployment from queue.");

    queue_count -= 1;
    queue_index = (queue_index + 1) % DEPLOYMENT_QUEUE_SIZE;
}
