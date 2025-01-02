// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "dependency_resolver.h"
#include "component_manager.h"
#include "dataplane_manager.h"
#include "deployment_handler.h"
#include "deployment_model.h"
#include <sys/types.h>
#include <assert.h>
#include <fcntl.h>
#include <ggl/base64.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/json_decode.h>
#include <ggl/list.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/recipe.h>
#include <ggl/semver.h>
#include <ggl/vector.h>
#include <limits.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

static GglError find_components_to_resolve(
    GglMap root_components, GglKVVec *components_to_resolve
) {
    GGL_MAP_FOREACH(pair, root_components) {
        if (pair->val.type != GGL_TYPE_MAP) {
            GGL_LOGE("Incorrect formatting for deployment components field.");
            return GGL_ERR_INVALID;
        }

        GglObject *val;
        GglBuffer component_version = { 0 };
        if (ggl_map_get(pair->val.map, GGL_STR("version"), &val)) {
            if (val->type != GGL_TYPE_BUF) {
                GGL_LOGE("Received invalid argument.");
                return GGL_ERR_INVALID;
            }
            component_version = val->buf;
        }

        if (ggl_buffer_eq(pair->key, GGL_STR("aws.greengrass.NucleusLite"))) {
            GglBuffer software_version = GGL_STR(GGL_VERSION);
            if (!ggl_buffer_eq(component_version, software_version)) {
                GGL_LOGE(
                    "The deployment failed. The aws.greengrass.NucleusLite "
                    "component version specified in the deployment is %.*s, "
                    "but the version of the GG Lite software is %.*s. Please "
                    "ensure that the version in the deployment matches before "
                    "attempting the deployment again.",
                    (int) component_version.len,
                    component_version.data,
                    (int) software_version.len,
                    software_version.data
                );
                return GGL_ERR_INVALID;
            }
        }

        GglError ret = ggl_kv_vec_push(
            components_to_resolve,
            (GglKV) { pair->key, GGL_OBJ_BUF(component_version) }
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }
    return GGL_ERR_OK;
}

static GglError find_root_components_to_resolve(
    GglBuffer thing_group_name,
    GglKVVec *components_to_resolve,
    GglBumpAlloc *version_requirements_balloc,
    GglAlloc *alloc
) {
    GglObject group_root_components_read_value;
    GglError ret = ggl_gg_config_read(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("DeploymentService"),
            GGL_STR("thingGroupsToRootComponents"),
            thing_group_name
        ),
        alloc,
        &group_root_components_read_value
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGI(
            "No info found in config for root components for thing "
            "group %.*s, assuming no components are part of this thing "
            "group.",
            (int) thing_group_name.len,
            thing_group_name.data
        );
    } else {
        if (group_root_components_read_value.type != GGL_TYPE_MAP) {
            GGL_LOGE("Did not read a map from config for thing group to "
                     "root components map");
            return GGL_ERR_INVALID;
        }

        GGL_MAP_FOREACH(
            root_component_pair, group_root_components_read_value.map
        ) {
            // If component is already in the root component list, it
            // must be the same version as the one already in the list
            // or we have a conflict.
            GglObject *existing_root_component_version;
            ret = ggl_map_validate(
                components_to_resolve->map,
                GGL_MAP_SCHEMA(
                    { root_component_pair->key,
                      false,
                      GGL_TYPE_BUF,
                      &existing_root_component_version },
                )
            );
            if (ret != GGL_ERR_OK) {
                return ret;
            }

            bool need_to_add_root_component = true;

            if (existing_root_component_version != NULL) {
                if (ggl_buffer_eq(
                        existing_root_component_version->buf,
                        root_component_pair->val.buf
                    )) {
                    need_to_add_root_component = false;
                } else {
                    GGL_LOGE(
                        "There is a version conflict for component "
                        "%.*s, where two deployments are asking for "
                        "versions %.*s and %.*s. Please check that "
                        "this root component does not have conflicting "
                        "versions across your deployments.",
                        (int) root_component_pair->key.len,
                        root_component_pair->key.data,
                        (int) root_component_pair->val.buf.len,
                        root_component_pair->val.buf.data,
                        (int) existing_root_component_version->buf.len,
                        existing_root_component_version->buf.data
                    );
                    return GGL_ERR_INVALID;
                }
            }

            if (need_to_add_root_component) {
                GglBuffer root_component_name_buf;
                ret = ggl_buf_clone(
                    root_component_pair->key, alloc, &root_component_name_buf
                );
                if (ret != GGL_ERR_OK) {
                    return ret;
                }

                GglBuffer root_component_version_buf;
                ret = ggl_buf_clone(
                    root_component_pair->val.buf,
                    &version_requirements_balloc->alloc,
                    &root_component_version_buf
                );
                if (ret != GGL_ERR_OK) {
                    return ret;
                }

                ret = ggl_kv_vec_push(
                    components_to_resolve,
                    (GglKV) { root_component_name_buf,
                              GGL_OBJ_BUF(root_component_version_buf) }
                );
                if (ret != GGL_ERR_OK) {
                    GGL_LOGE(
                        "Failed to add %.*s version %.*s to the list of "
                        "components to resolve.",
                        (int) root_component_name_buf.len,
                        root_component_name_buf.data,
                        (int) root_component_version_buf.len,
                        root_component_version_buf.data
                    );
                    return ret;
                }
                GGL_LOGD(
                    "Added %.*s to the list of root components to "
                    "resolve "
                    "from "
                    "the thing group %.*s",
                    (int) root_component_name_buf.len,
                    root_component_name_buf.data,
                    (int) thing_group_name.len,
                    thing_group_name.data
                );
            }
        }
    }
    return GGL_ERR_OK;
}

static GglError parse_thing_groups_response(
    GglBuffer thing_group_name,
    GglBuffer list_thing_groups_response,
    GglKVVec *components_to_resolve,
    GglBumpAlloc *version_requirements_balloc,
    GglAlloc *alloc
) {
    // TODO: Add a schema and only parse the fields we need to save memory
    GglObject json_thing_groups_object;
    uint8_t thing_groups_response_mem[100 * sizeof(GglObject)];
    GglBumpAlloc thing_groups_json_balloc
        = ggl_bump_alloc_init(GGL_BUF(thing_groups_response_mem));
    GglError ret = ggl_json_decode_destructive(
        list_thing_groups_response,
        &thing_groups_json_balloc.alloc,
        &json_thing_groups_object
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Error when parsing listThingGroups response to "
                 "json.");
        return ret;
    }

    if (json_thing_groups_object.type != GGL_TYPE_MAP) {
        GGL_LOGE("listThingGroups response did not parse into a "
                 "map.");
        return ret;
    }

    GglObject *thing_groups_list;
    if (!ggl_map_get(
            json_thing_groups_object.map,
            GGL_STR("thingGroups"),
            &thing_groups_list
        )) {
        GGL_LOGE("Missing thingGroups.");
        return ret;
    }
    if (thing_groups_list->type != GGL_TYPE_LIST) {
        GGL_LOGE("thingGroups response is not a list.");
        return ret;
    }

    GGL_LIST_FOREACH(thing_group_item, thing_groups_list->list) {
        if (thing_group_item->type != GGL_TYPE_MAP) {
            GGL_LOGE("Thing group item is not of type map.");
            return ret;
        }

        GglObject *thing_group_name_from_item;

        ret = ggl_map_validate(
            thing_group_item->map,
            GGL_MAP_SCHEMA(
                { GGL_STR("thingGroupName"),
                  true,
                  GGL_TYPE_BUF,
                  &thing_group_name_from_item },
            )
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        if (ggl_buffer_eq(thing_group_name_from_item->buf, thing_group_name)) {
            continue;
        }

        ret = find_root_components_to_resolve(
            thing_group_name_from_item->buf,
            components_to_resolve,
            version_requirements_balloc,
            alloc
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }
    return GGL_ERR_OK;
}

static GglError parse_dataplane_response_and_save_recipe(
    GglBuffer dataplane_response,
    GglDeploymentHandlerThreadArgs *args,
    GglBuffer *cloud_version
) {
    GglObject json_candidates_response_obj;
    // TODO: Figure out a better size. This response can be big.
    uint8_t candidates_response_mem[100 * sizeof(GglObject)];
    GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(candidates_response_mem));
    GglError ret = ggl_json_decode_destructive(
        dataplane_response, &balloc.alloc, &json_candidates_response_obj
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Error when parsing resolveComponentCandidates response to "
                 "json.");
        return ret;
    }

    if (json_candidates_response_obj.type != GGL_TYPE_MAP) {
        GGL_LOGE("resolveComponentCandidates response did not parse into a "
                 "map.");
        return ret;
    }

    GglObject *resolved_component_versions;
    if (!ggl_map_get(
            json_candidates_response_obj.map,
            GGL_STR("resolvedComponentVersions"),
            &resolved_component_versions
        )) {
        GGL_LOGE("Missing resolvedComponentVersions.");
        return ret;
    }
    if (resolved_component_versions->type != GGL_TYPE_LIST) {
        GGL_LOGE("resolvedComponentVersions response is not a list.");
        return ret;
    }

    bool first_component = true;
    GGL_LIST_FOREACH(resolved_version, resolved_component_versions->list) {
        if (!first_component) {
            GGL_LOGE(
                "resolveComponentCandidates returned information for more than "
                "one component."
            );
            return GGL_ERR_INVALID;
        }
        first_component = false;

        if (resolved_version->type != GGL_TYPE_MAP) {
            GGL_LOGE("Resolved version is not of type map.");
            return ret;
        }

        GglObject *cloud_component_arn;
        GglObject *cloud_component_name;
        GglObject *cloud_component_version;
        GglObject *vendor_guidance;
        GglObject *recipe_file_content;

        ret = ggl_map_validate(
            resolved_version->map,
            GGL_MAP_SCHEMA(
                { GGL_STR("arn"), true, GGL_TYPE_BUF, &cloud_component_arn },
                { GGL_STR("componentName"),
                  true,
                  GGL_TYPE_BUF,
                  &cloud_component_name },
                { GGL_STR("componentVersion"),
                  true,
                  GGL_TYPE_BUF,
                  &cloud_component_version },
                { GGL_STR("vendorGuidance"),
                  false,
                  GGL_TYPE_BUF,
                  &vendor_guidance },
                { GGL_STR("recipe"), true, GGL_TYPE_BUF, &recipe_file_content },
            )
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        assert(cloud_component_version->buf.len <= NAME_MAX);

        memcpy(
            cloud_version->data,
            cloud_component_version->buf.data,
            cloud_component_version->buf.len
        );
        cloud_version->len = cloud_component_version->buf.len;

        if (vendor_guidance != NULL) {
            if (ggl_buffer_eq(vendor_guidance->buf, GGL_STR("DISCONTINUED"))) {
                GGL_LOGW("The component version has been discontinued by "
                         "its "
                         "publisher. You can deploy this component version, "
                         "but "
                         "we recommend that you use a different version of "
                         "this "
                         "component");
            }
        }

        if (recipe_file_content->buf.len == 0) {
            GGL_LOGE("Recipe is empty.");
        }

        ggl_base64_decode_in_place(&recipe_file_content->buf);
        recipe_file_content->buf.data[recipe_file_content->buf.len] = '\0';

        GGL_LOGD(
            "Decoded recipe data as: %.*s",
            (int) recipe_file_content->buf.len,
            recipe_file_content->buf.data
        );

        static uint8_t recipe_name_buf[PATH_MAX];
        GglByteVec recipe_name_vec = GGL_BYTE_VEC(recipe_name_buf);
        ret = ggl_byte_vec_append(&recipe_name_vec, cloud_component_name->buf);
        ggl_byte_vec_chain_append(&ret, &recipe_name_vec, GGL_STR("-"));
        ggl_byte_vec_chain_append(
            &ret, &recipe_name_vec, cloud_component_version->buf
        );
        // TODO: Actual support for .json files. We're writing a .json
        // to a .yaml and relying on yaml being an almost-superset of
        // json.
        ggl_byte_vec_chain_append(&ret, &recipe_name_vec, GGL_STR(".yaml"));
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to create recipe file name.");
            return ret;
        }

        static uint8_t recipe_dir_buf[PATH_MAX];
        GglByteVec recipe_dir_vec = GGL_BYTE_VEC(recipe_dir_buf);
        ret = ggl_byte_vec_append(
            &recipe_dir_vec,
            ggl_buffer_from_null_term((char *) args->root_path.data)
        );
        ggl_byte_vec_chain_append(
            &ret, &recipe_dir_vec, GGL_STR("/packages/recipes/")
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to create recipe directory name.");
            return ret;
        }

        // Write file
        int root_dir_fd = -1;
        ret = ggl_dir_open(recipe_dir_vec.buf, O_PATH, true, &root_dir_fd);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to open dir when writing cloud recipe.");
            return ret;
        }

        int fd = -1;
        ret = ggl_file_openat(
            root_dir_fd,
            recipe_name_vec.buf,
            O_CREAT | O_WRONLY | O_TRUNC,
            (mode_t) 0644,
            &fd
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to open file at the dir when writing cloud "
                     "recipe.");
            return ret;
        }

        ret = ggl_file_write(fd, recipe_file_content->buf);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Write to cloud recipe file failed");
            return ret;
        }

        GGL_LOGD("Saved recipe under the name %s", recipe_name_vec.buf.data);

        ret = ggl_gg_config_write(
            GGL_BUF_LIST(GGL_STR("services"), cloud_component_name->buf, ),
            GGL_OBJ_MAP(GGL_MAP({ GGL_STR("arn"), *cloud_component_arn })),
            &(int64_t) { 1 }
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Write of arn to config failed");
            return ret;
        }
    }

    return GGL_ERR_OK;
}

static GglError evaluate_component_dependencies(
    GglDeploymentHandlerThreadArgs *args,
    GglBuffer resolved_version,
    GglBuffer component_name,
    GglKVVec *resolved_components_kv_vec,
    GglKVVec *components_to_resolve,
    GglBumpAlloc *version_requirements_balloc,
    GglAlloc *alloc
) {
    // Get actual recipe read
    GglObject recipe_obj;
    static uint8_t recipe_mem[MAX_RECIPE_MEM] = { 0 };
    GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(recipe_mem));
    GglError ret = ggl_recipe_get_from_file(
        args->root_path_fd,
        component_name,
        resolved_version,
        &balloc.alloc,
        &recipe_obj
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    GglObject *component_dependencies = NULL;

    if (recipe_obj.type != GGL_TYPE_MAP) {
        GGL_LOGE("Recipe object did not parse into a map.");
        return GGL_ERR_INVALID;
    }

    ret = ggl_map_validate(
        recipe_obj.map,
        GGL_MAP_SCHEMA(
            { GGL_STR("ComponentDependencies"),
              false,
              GGL_TYPE_MAP,
              &component_dependencies },
        )
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    if (!component_dependencies) {
        // no dependencies found
        return GGL_ERR_OK;
    }

    GGL_MAP_FOREACH(dependency, component_dependencies->map) {
        if (dependency->val.type != GGL_TYPE_MAP) {
            GGL_LOGE("Component dependency in recipe does not have map data");
            return GGL_ERR_INVALID;
        }

        // If the component is aws.greengrass.Nucleus or
        // aws.greengrass.TokenExchangeService ignore it and never add
        // it as a dependency to check or parse.
        if (ggl_buffer_eq(dependency->key, GGL_STR("aws.greengrass.Nucleus"))
            || ggl_buffer_eq(
                dependency->key, GGL_STR("aws.greengrass.TokenExchangeService")
            )) {
            GGL_LOGD(
                "Skipping a dependency during resolution as it is %.*s",
                (int) dependency->key.len,
                dependency->key.data
            );
            continue;
        }

        GglObject *dep_version_requirement = NULL;
        ret = ggl_map_validate(
            dependency->val.map,
            GGL_MAP_SCHEMA(
                { GGL_STR("VersionRequirement"),
                  true,
                  GGL_TYPE_BUF,
                  &dep_version_requirement },
            )
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        // If we already resolved the component version, check that it
        // still satisfies the new requirement and fail otherwise.
        GglObject *already_resolved_version;
        ret = ggl_map_validate(
            resolved_components_kv_vec->map,
            GGL_MAP_SCHEMA(
                { dependency->key,
                  false,
                  GGL_TYPE_BUF,
                  &already_resolved_version },
            )
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        if (already_resolved_version) {
            bool meets_requirements = is_in_range(
                already_resolved_version->buf, dep_version_requirement->buf
            );
            if (!meets_requirements) {
                GGL_LOGE("Already resolved component does not meet new "
                         "dependency requirement, failing dependency "
                         "resolution.");
                return GGL_ERR_FAILURE;
            }
            continue;
        }

        // If we haven't resolved it yet, check if we have an
        // existing requirement and append the new requirement if
        // so.
        GglObject *existing_requirements;
        ret = ggl_map_validate(
            components_to_resolve->map,
            GGL_MAP_SCHEMA(
                { dependency->key,
                  false,
                  GGL_TYPE_BUF,
                  &existing_requirements },
            )
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        if (existing_requirements) {
            uint8_t new_req_buf[PATH_MAX];
            GglByteVec new_req_vec = GGL_BYTE_VEC(new_req_buf);
            ret = ggl_byte_vec_append(&new_req_vec, existing_requirements->buf);
            ggl_byte_vec_chain_push(&ret, &new_req_vec, ' ');
            ggl_byte_vec_chain_append(
                &ret, &new_req_vec, dep_version_requirement->buf
            );
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Failed to create new requirements for "
                         "dependency version.");
                return ret;
            }

            uint8_t *new_req = GGL_ALLOCN(
                &version_requirements_balloc->alloc,
                uint8_t,
                new_req_vec.buf.len
            );
            if (new_req == NULL) {
                GGL_LOGE("Ran out of memory while trying to create "
                         "new requirements");
                return GGL_ERR_NOMEM;
            }

            memcpy(new_req, new_req_vec.buf.data, new_req_vec.buf.len);
            *existing_requirements = GGL_OBJ_BUF((GglBuffer
            ) { .data = new_req, .len = new_req_vec.buf.len });
        } else {
            // If we haven't resolved it yet, and it doesn't have an
            // existing requirement, add it.
            GglBuffer name_key_buf;
            ret = ggl_buf_clone(dependency->key, alloc, &name_key_buf);
            if (ret != GGL_ERR_OK) {
                return ret;
            }

            GglBuffer vers_key_buf;
            ret = ggl_buf_clone(
                dep_version_requirement->buf,
                &version_requirements_balloc->alloc,
                &vers_key_buf
            );
            if (ret != GGL_ERR_OK) {
                return ret;
            }

            ret = ggl_kv_vec_push(
                components_to_resolve,
                (GglKV) { name_key_buf, GGL_OBJ_BUF(vers_key_buf) }
            );
            if (ret != GGL_ERR_OK) {
                return ret;
            }
        }
    }
    return GGL_ERR_OK;
}

GglError resolve_dependencies(
    GglMap root_components,
    GglBuffer thing_group_name,
    GglDeploymentHandlerThreadArgs *args,
    GglAlloc *alloc,
    GglKVVec *resolved_components_kv_vec
) {
    GglError ret;

    // TODO: Decide on size
    GglKVVec components_to_resolve = GGL_KV_VEC((GglKV[64]) { 0 });

    static uint8_t version_requirements_mem[2048] = { 0 };
    GglBumpAlloc version_requirements_balloc
        = ggl_bump_alloc_init(GGL_BUF(version_requirements_mem));

    // Root components from current deployment
    ret = find_components_to_resolve(root_components, &components_to_resolve);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to find components needing resolution.");
        return ret;
    }

    // At this point, components_to_resolve should be only a map of root
    // component names to their version requirements from the deployment. This
    // may be empty! We delete the key first in case components were removed.
    ret = ggl_gg_config_delete(GGL_BUF_LIST(
        GGL_STR("services"),
        GGL_STR("DeploymentService"),
        GGL_STR("thingGroupsToRootComponents"),
        thing_group_name
    ));

    if (ret != GGL_ERR_OK) {
        GGL_LOGW(
            "Error while deleting thing group to root components mapping for "
            "thing group %.*s",
            (int) thing_group_name.len,
            thing_group_name.data
        );
        return ret;
    }
    ret = ggl_gg_config_write(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("DeploymentService"),
            GGL_STR("thingGroupsToRootComponents"),
            thing_group_name
        ),
        GGL_OBJ_MAP(components_to_resolve.map),
        0
    );

    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "Failed to write thing group to root components map to ggconfigd."
        );
        return ret;
    }

    // Get list of thing groups
    static uint8_t list_thing_groups_response_buf[2048] = { 0 };
    GglBuffer list_thing_groups_response
        = GGL_BUF(list_thing_groups_response_buf);

    ret = get_device_thing_groups(&list_thing_groups_response);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = parse_thing_groups_response(
        thing_group_name,
        list_thing_groups_response,
        &components_to_resolve,
        &version_requirements_balloc,
        alloc
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    // Add local components to components to resolve, if it isn't a local
    // deployment
    if (!ggl_buffer_eq(GGL_STR("LOCAL_DEPLOYMENTS"), thing_group_name)) {
        ret = find_root_components_to_resolve(
            GGL_STR("LOCAL_DEPLOYMENTS"),
            &components_to_resolve,
            &version_requirements_balloc,
            alloc
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    GGL_MAP_FOREACH(pair, components_to_resolve.map) {
        // We assume that we have not resolved a component yet if we are finding
        // it in this map.
        uint8_t resolved_version_arr[NAME_MAX];
        GglBuffer resolved_version = GGL_BUF(resolved_version_arr);
        bool found_local_candidate = resolve_component_version(
            pair->key, pair->val.buf, &resolved_version
        );

        if (!found_local_candidate) {
            // Resolve with cloud and download recipe
            static uint8_t resolve_component_candidates_response_buf[16384]
                = { 0 };
            GglBuffer resolve_component_candidates_response
                = GGL_BUF(resolve_component_candidates_response_buf);

            ret = resolve_component_with_cloud(
                pair->key, pair->val.buf, &resolve_component_candidates_response
            );
            if (ret != GGL_ERR_OK) {
                return ret;
            }

            bool is_empty_response = ggl_buffer_eq(
                resolve_component_candidates_response, GGL_STR("{}")
            );

            if (is_empty_response) {
                GGL_LOGI(
                    "Cloud version resolution failed for component %.*s.",
                    (int) pair->key.len,
                    pair->val.buf.data
                );
                return GGL_ERR_FAILURE;
            }

            ret = parse_dataplane_response_and_save_recipe(
                resolve_component_candidates_response, args, &resolved_version
            );
            if (ret != GGL_ERR_OK) {
                return ret;
            }
        }

        // Add resolved component to list of resolved components
        GglBuffer val_buf;
        ret = ggl_buf_clone(resolved_version, alloc, &val_buf);
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        ret = ggl_kv_vec_push(
            resolved_components_kv_vec,
            (GglKV) { pair->key, GGL_OBJ_BUF(val_buf) }
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "Error while adding component to list of resolved component"
            );
            return ret;
        }

        // Find dependencies from recipe and add them to the list of components
        // to resolve. If the dependency is for a component that is already
        // resolved, verify that new requirements are satisfied and fail
        // deployment if not.
        ret = evaluate_component_dependencies(
            args,
            resolved_version,
            pair->key,
            resolved_components_kv_vec,
            &components_to_resolve,
            &version_requirements_balloc,
            alloc
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "Failed to evaluate dependencies for %.*s.",
                (int) pair->key.len,
                pair->key.data
            );
            return ret;
        }
    }

    return GGL_ERR_OK;
}
