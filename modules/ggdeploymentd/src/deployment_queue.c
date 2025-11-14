// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "deployment_queue.h"
#include "deployment_model.h"
#include <assert.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/flags.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/vector.h>
#include <ggl/core_bus/gg_config.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
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

static bool get_matching_deployment(GgBuffer deployment_id, size_t *index) {
    for (size_t i = 0; i < queue_count; i++) {
        size_t index_i = (queue_index + i) % DEPLOYMENT_QUEUE_SIZE;
        if (gg_buffer_eq(deployment_id, deployments[index_i].deployment_id)) {
            *index = index_i;
            return true;
        }
    }
    return false;
}

static GgError null_terminate_buffer(GgBuffer *buf, GgArena *alloc) {
    if (buf->len == 0) {
        *buf = GG_STR("");
        return GG_ERR_OK;
    }

    uint8_t *mem = GG_ARENA_ALLOCN(alloc, uint8_t, buf->len + 1);
    if (mem == NULL) {
        GG_LOGE("Failed to allocate memory for copying buffer.");
        return GG_ERR_NOMEM;
    }

    memcpy(mem, buf->data, buf->len);
    mem[buf->len] = '\0';
    buf->data = mem;
    return GG_ERR_OK;
}

GgError deep_copy_deployment(GglDeployment *deployment, GgArena *alloc) {
    assert(deployment != NULL);

    GgObject obj = gg_obj_buf(deployment->deployment_id);
    GgError ret = gg_arena_claim_obj(&obj, alloc);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    deployment->deployment_id = gg_obj_into_buf(obj);

    ret = null_terminate_buffer(&deployment->recipe_directory_path, alloc);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ret = null_terminate_buffer(&deployment->artifacts_directory_path, alloc);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    obj = gg_obj_map(deployment->components);
    ret = gg_arena_claim_obj(&obj, alloc);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    deployment->components = gg_obj_into_map(obj);

    obj = gg_obj_buf(deployment->configuration_arn);
    ret = gg_arena_claim_obj(&obj, alloc);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    deployment->configuration_arn = gg_obj_into_buf(obj);

    obj = gg_obj_buf(deployment->thing_group);
    ret = gg_arena_claim_obj(&obj, alloc);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    deployment->thing_group = gg_obj_into_buf(obj);

    return GG_ERR_OK;
}

static void get_slash_and_colon_locations_from_arn(
    GgBuffer arn, size_t *slash_index, size_t *last_colon_index
) {
    assert(*slash_index == 0);
    assert(*last_colon_index == 0);
    for (size_t i = arn.len; i > 0; i--) {
        if (arn.data[i - 1] == ':') {
            if (*last_colon_index == 0) {
                *last_colon_index = i - 1;
            }
        }
        if (arn.data[i - 1] == '/') {
            *slash_index = i - 1;
        }
        if (*slash_index != 0 && *last_colon_index != 0) {
            break;
        }
    }
}

static GgError parse_deployment_obj(
    GgMap args,
    GglDeployment *doc,
    GglDeploymentType type,
    GgArena *alloc,
    GgKVVec *local_components_kv_vec
) {
    *doc = (GglDeployment) { 0 };

    GgObject *recipe_directory_path;
    GgObject *artifacts_directory_path;
    GgObject *root_component_versions_to_add;
    GgObject *cloud_components;
    GgObject *deployment_id;
    GgObject *configuration_arn_obj;

    GgError ret = gg_map_validate(
        args,
        GG_MAP_SCHEMA(
            { GG_STR("recipe_directory_path"),
              GG_OPTIONAL,
              GG_TYPE_BUF,
              &recipe_directory_path },
            { GG_STR("artifacts_directory_path"),
              GG_OPTIONAL,
              GG_TYPE_BUF,
              &artifacts_directory_path },
            { GG_STR("root_component_versions_to_add"),
              GG_OPTIONAL,
              GG_TYPE_MAP,
              &root_component_versions_to_add },
            { GG_STR("components"),
              GG_OPTIONAL,
              GG_TYPE_MAP,
              &cloud_components },
            { GG_STR("deploymentId"),
              GG_OPTIONAL,
              GG_TYPE_BUF,
              &deployment_id },
            { GG_STR("configurationArn"),
              GG_OPTIONAL,
              GG_TYPE_BUF,
              &configuration_arn_obj },
        )
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Received invalid argument.");
        return GG_ERR_INVALID;
    }

    if (recipe_directory_path != NULL) {
        doc->recipe_directory_path = gg_obj_into_buf(*recipe_directory_path);
    }

    if (artifacts_directory_path != NULL) {
        doc->artifacts_directory_path
            = gg_obj_into_buf(*artifacts_directory_path);
    }

    if (deployment_id != NULL) {
        doc->deployment_id = gg_obj_into_buf(*deployment_id);
    } else {
        static uint8_t uuid_mem[37];
        uuid_t binuuid;
        uuid_generate_random(binuuid);
        uuid_unparse(binuuid, (char *) uuid_mem);
        doc->deployment_id = (GgBuffer) { .data = uuid_mem, .len = 36 };
    }

    if (type == THING_GROUP_DEPLOYMENT) {
        if (cloud_components != NULL) {
            doc->components = gg_obj_into_map(*cloud_components);
        } else {
            GG_LOGW(
                "Deployment is of type thing group deployment but does not have component information."
            );
        }

        if (configuration_arn_obj != NULL) {
            // Assume that the arn has a version at the end, we want to discard
            // the version for the arn.
            GgBuffer configuration_arn
                = gg_obj_into_buf(*configuration_arn_obj);
            size_t last_colon_index = 0;
            size_t slash_index = 0;
            get_slash_and_colon_locations_from_arn(
                configuration_arn, &slash_index, &last_colon_index
            );
            doc->configuration_arn = configuration_arn;
            doc->thing_group = gg_buffer_substr(
                configuration_arn, slash_index + 1, last_colon_index
            );
        }
    }

    if (type == LOCAL_DEPLOYMENT) {
        doc->thing_group = GG_STR("LOCAL_DEPLOYMENTS");
        doc->configuration_arn = doc->deployment_id;

        GgObject local_deployment_root_components_read_value;
        ret = ggl_gg_config_read(
            GG_BUF_LIST(
                GG_STR("services"),
                GG_STR("DeploymentService"),
                GG_STR("thingGroupsToRootComponents"),
                GG_STR("LOCAL_DEPLOYMENTS")
            ),
            alloc,
            &local_deployment_root_components_read_value
        );
        if (ret != GG_ERR_OK) {
            GG_LOGI(
                "No info found in config for root components for local deployments, assuming no components have been deployed locally yet."
            );
            // If no components existed in past deployments, then there is
            // nothing to remove and the list of components for local deployment
            // is just components to add.
            GG_MAP_FOREACH (
                component_pair, gg_obj_into_map(*root_component_versions_to_add)
            ) {
                if (gg_obj_type(*gg_kv_val(component_pair)) != GG_TYPE_BUF) {
                    GG_LOGE(
                        "Local deployment component version read incorrectly from the deployment doc."
                    );
                    return GG_ERR_INVALID;
                }

                // TODO: Add configurationUpdate and runWith
                GgKV *new_component_info_mem = GG_ARENA_ALLOC(alloc, GgKV);
                if (new_component_info_mem == NULL) {
                    GG_LOGE(
                        "No memory when allocating memory while enqueuing local deployment."
                    );
                    return GG_ERR_NOMEM;
                }
                *new_component_info_mem
                    = gg_kv(GG_STR("version"), *gg_kv_val(component_pair));
                GgMap new_component_info_map
                    = (GgMap) { .pairs = new_component_info_mem, .len = 1 };

                ret = gg_kv_vec_push(
                    local_components_kv_vec,
                    gg_kv(
                        gg_kv_key(*component_pair),
                        gg_obj_map(new_component_info_map)
                    )
                );
                if (ret != GG_ERR_OK) {
                    return ret;
                }
            }

            doc->components = local_components_kv_vec->map;
        } else {
            if (gg_obj_type(local_deployment_root_components_read_value)
                != GG_TYPE_MAP) {
                GG_LOGE(
                    "Local deployment component list read incorrectly from the config."
                );
                return GG_ERR_INVALID;
            }
            // Pre-populate with all local components that already have been
            // deployed
            GG_MAP_FOREACH (
                old_component_pair,
                gg_obj_into_map(local_deployment_root_components_read_value)
            ) {
                if (gg_obj_type(*gg_kv_val(old_component_pair))
                    != GG_TYPE_BUF) {
                    GG_LOGE(
                        "Local deployment component version read incorrectly from the config."
                    );
                    return GG_ERR_INVALID;
                }

                GG_LOGD(
                    "Found existing local component %.*s as part of local deployments group.",
                    (int) gg_kv_key(*old_component_pair).len,
                    gg_kv_key(*old_component_pair).data
                );

                GgKV *old_component_info_mem = GG_ARENA_ALLOC(alloc, GgKV);
                if (old_component_info_mem == NULL) {
                    GG_LOGE(
                        "No memory when allocating memory while enqueuing local deployment."
                    );
                    return GG_ERR_NOMEM;
                }
                *old_component_info_mem
                    = gg_kv(GG_STR("version"), *gg_kv_val(old_component_pair));
                GgMap old_component_info_map
                    = (GgMap) { .pairs = old_component_info_mem, .len = 1 };

                ret = gg_kv_vec_push(
                    local_components_kv_vec,
                    gg_kv(
                        gg_kv_key(*old_component_pair),
                        gg_obj_map(old_component_info_map)
                    )
                );
                if (ret != GG_ERR_OK) {
                    return ret;
                }
            }

            // Add the component to add to the existing list of locally deployed
            // components, or update the version if it already exists.
            GG_MAP_FOREACH (
                component_pair, gg_obj_into_map(*root_component_versions_to_add)
            ) {
                if (gg_obj_type(*gg_kv_val(component_pair)) != GG_TYPE_BUF) {
                    GG_LOGE(
                        "Local deployment component version read incorrectly from the deployment doc."
                    );
                    return GG_ERR_INVALID;
                }

                GgObject *existing_component_data;
                // TODO: Remove component if it is in the removal list.
                if (!gg_map_get(
                        local_components_kv_vec->map,
                        gg_kv_key(*component_pair),
                        &existing_component_data
                    )) {
                    GG_LOGD(
                        "Locally deployed component not previously deployed, adding it to the list of local components."
                    );
                    // TODO: Add configurationUpdate and runWith
                    GgKV *new_component_info_mem = GG_ARENA_ALLOC(alloc, GgKV);
                    if (new_component_info_mem == NULL) {
                        GG_LOGE(
                            "No memory when allocating memory while enqueuing local deployment."
                        );
                        return GG_ERR_NOMEM;
                    }
                    *new_component_info_mem
                        = gg_kv(GG_STR("version"), *gg_kv_val(component_pair));
                    GgMap new_component_info_map
                        = (GgMap) { .pairs = new_component_info_mem, .len = 1 };

                    ret = gg_kv_vec_push(
                        local_components_kv_vec,
                        gg_kv(
                            gg_kv_key(*component_pair),
                            gg_obj_map(new_component_info_map)
                        )
                    );
                    if (ret != GG_ERR_OK) {
                        return ret;
                    }
                } else {
                    GgKV *new_component_info_mem = GG_ARENA_ALLOC(alloc, GgKV);
                    if (new_component_info_mem == NULL) {
                        GG_LOGE(
                            "No memory when allocating memory while enqueuing local deployment."
                        );
                        return GG_ERR_NOMEM;
                    }
                    *new_component_info_mem
                        = gg_kv(GG_STR("version"), *gg_kv_val(component_pair));
                    GgMap new_component_info_map
                        = (GgMap) { .pairs = new_component_info_mem, .len = 1 };
                    *existing_component_data
                        = gg_obj_map(new_component_info_map);
                }
            }

            doc->components = local_components_kv_vec->map;
        }
    }

    return GG_ERR_OK;
}

GgError ggl_deployment_enqueue(
    GgMap deployment_doc, GgByteVec *id, GglDeploymentType type
) {
    GG_MTX_SCOPE_GUARD(&queue_mtx);

    // We are reading a map that may contain MAX_LOCAL_COMPONENTS names to
    // version mappings. This mem is limited to this function call but we deep
    // copy into static memory later in this function.
    uint8_t local_deployment_shortlived_balloc_buf
        [(1 + 2 * MAX_LOCAL_COMPONENTS) * sizeof(GgObject)];
    GgArena shortlived_alloc
        = gg_arena_init(GG_BUF(local_deployment_shortlived_balloc_buf));
    GglDeployment new = { 0 };
    GgKVVec local_components_kv_vec
        = GG_KV_VEC((GgKV[MAX_LOCAL_COMPONENTS]) { 0 });
    GgError ret = parse_deployment_obj(
        deployment_doc, &new, type, &shortlived_alloc, &local_components_kv_vec
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    new.type = type;

    if (id != NULL) {
        ret = gg_byte_vec_append(id, new.deployment_id);
        if (ret != GG_ERR_OK) {
            GG_LOGE("insufficient id length");
            return ret;
        }
    }

    new.state = GGL_DEPLOYMENT_QUEUED;

    size_t index;
    bool exists = get_matching_deployment(new.deployment_id, &index);
    if (exists) {
        if (deployments[index].state != GGL_DEPLOYMENT_QUEUED) {
            GG_LOGI("Existing deployment not replaceable.");
            return GG_ERR_OK;
        }
        GG_LOGI("Replacing existing deployment in queue.");
    } else {
        if (queue_count >= DEPLOYMENT_QUEUE_SIZE) {
            return GG_ERR_BUSY;
        }

        GG_LOGD("Adding a new deployment to the queue.");
        index = (queue_index + queue_count) % DEPLOYMENT_QUEUE_SIZE;
        queue_count += 1;
    }

    GgArena alloc = gg_arena_init(GG_BUF(deployment_mem[index]));
    ret = deep_copy_deployment(&new, &alloc);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    deployments[index] = new;

    pthread_cond_signal(&notify_cond);

    return GG_ERR_OK;
}

GgError ggl_deployment_dequeue(GglDeployment **deployment) {
    GG_MTX_SCOPE_GUARD(&queue_mtx);

    while (queue_count == 0) {
        pthread_cond_wait(&notify_cond, &queue_mtx);
    }

    deployments[queue_index].state = GGL_DEPLOYMENT_IN_PROGRESS;
    *deployment = &deployments[queue_index];

    GG_LOGD("Set a deployment to in progress.");

    return GG_ERR_OK;
}

void ggl_deployment_release(GglDeployment *deployment) {
    GG_MTX_SCOPE_GUARD(&queue_mtx);

    assert(gg_buffer_eq(
        deployment->deployment_id, deployments[queue_index].deployment_id
    ));

    GG_LOGD("Removing deployment from queue.");

    queue_count -= 1;
    queue_index = (queue_index + 1) % DEPLOYMENT_QUEUE_SIZE;
}
