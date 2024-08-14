// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../ipc_server.h"
#include "handlers.h"
#include <ggl/alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <sys/time.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

GglError handle_create_local_deployment(
    GglMap args, uint32_t handle, int32_t stream_id, GglAlloc *alloc
) {
    (void) alloc;

    GglObject *recipe_directory_path = NULL;
    GglKV found_args[8];
    int found_args_index = 0;
    bool recipe_directory_found = ggl_map_get(
        args, GGL_STR("recipeDirectoryPath"), &recipe_directory_path
    );
    if (recipe_directory_found) {
        if (recipe_directory_path->type != GGL_TYPE_BUF) {
            GGL_LOGE(
                "CreateLocalDeployment", "recipeDirectoryPath not a string."
            );
            return GGL_ERR_INVALID;
        }
        GglKV recipe_dir_kv = { .key = GGL_STR("recipe_directory_path"),
                                .val = *recipe_directory_path };
        found_args[found_args_index] = recipe_dir_kv;
        found_args_index++;
    }

    GglObject *artifacts_directory_path = NULL;
    bool artifacts_directory_found = ggl_map_get(
        args, GGL_STR("artifactsDirectoryPath"), &artifacts_directory_path
    );
    if (artifacts_directory_found) {
        if (artifacts_directory_path->type != GGL_TYPE_BUF) {
            GGL_LOGE(
                "CreateLocalDeployment", "artifactsDirectoryPath not a string."
            );
            return GGL_ERR_INVALID;
        }
        GglKV artifacts_dir_kv = { .key = GGL_STR("artifact_directory_path"),
                                   .val = *artifacts_directory_path };
        found_args[found_args_index] = artifacts_dir_kv;
        found_args_index++;
    }

    GglObject *component_to_version_map = NULL;
    bool root_component_version_found = ggl_map_get(
        args, GGL_STR("rootComponentVersionsToAdd"), &component_to_version_map
    );
    if (root_component_version_found) {
        if (component_to_version_map->type != GGL_TYPE_MAP) {
            GGL_LOGE(
                "CreateLocalDeployment",
                "rootComponentVersionsToAdd must be provided a map."
            );
            return GGL_ERR_INVALID;
        }
        GglKV component_to_version_kv
            = { .key = GGL_STR("root_component_versions_to_add"),
                .val = *component_to_version_map };
        found_args[found_args_index] = component_to_version_kv;
        found_args_index++;
    }

    GglObject *root_components_to_remove = NULL;
    bool root_component_to_remove_found = ggl_map_get(
        args, GGL_STR("rootComponentsToRemove"), &root_components_to_remove
    );
    if (root_component_to_remove_found) {
        if (root_components_to_remove->type != GGL_TYPE_LIST) {
            GGL_LOGE(
                "CreateLocalDeployment",
                "rootComponentsToRemove must be provided a list."
            );
            return GGL_ERR_INVALID;
        }
        GglKV root_components_remove_kv
            = { .key = GGL_STR("root_components_to_remove"),
                .val = *root_components_to_remove };
        found_args[found_args_index] = root_components_remove_kv;
        found_args_index++;
    }

    GglObject *component_to_configuration = NULL;
    bool component_to_configuration_found = ggl_map_get(
        args, GGL_STR("componentToConfiguration"), &component_to_configuration
    );
    if (component_to_configuration_found) {
        if (component_to_configuration->type != GGL_TYPE_MAP) {
            GGL_LOGE(
                "CreateLocalDeployment",
                "componentToConfiguration must be provided a map."
            );
            return GGL_ERR_INVALID;
        }
        GglKV component_to_configuration_kv
            = { .key = GGL_STR("component_to_configuration"),
                .val = *component_to_configuration };
        found_args[found_args_index] = component_to_configuration_kv;
        found_args_index++;
    }

    GglObject *component_to_run_with_info = NULL;
    bool component_to_run_with_info_found = ggl_map_get(
        args, GGL_STR("componentToRunWithInfo"), &component_to_run_with_info
    );
    if (component_to_run_with_info_found) {
        if (component_to_run_with_info->type != GGL_TYPE_MAP) {
            GGL_LOGE(
                "CreateLocalDeployment",
                "componentToRunWithInfo must be provided a map."
            );
            return GGL_ERR_INVALID;
        }
        GglKV component_to_run_with_info_kv
            = { .key = GGL_STR("component_to_run_with_info"),
                .val = *component_to_run_with_info };
        found_args[found_args_index] = component_to_run_with_info_kv;
        found_args_index++;
    }

    GglObject *group_name = NULL;
    bool group_name_found
        = ggl_map_get(args, GGL_STR("groupName"), &group_name);
    if (group_name_found) {
        if (group_name->type != GGL_TYPE_BUF) {
            GGL_LOGE("CreateLocalDeployment", "groupName not a string.");
            return GGL_ERR_INVALID;
        }
        GglKV group_name_kv
            = { .key = GGL_STR("group_name"), .val = *group_name };
        found_args[found_args_index] = group_name_kv;
        found_args_index++;
    }

    struct timeval time;
    gettimeofday(&time, NULL);
    int64_t millis_from_seconds = (int64_t) (time.tv_sec) * 1000;
    int64_t millis_from_microseconds = (time.tv_usec / 1000);
    GglObject timestamp
        = GGL_OBJ_I64(millis_from_seconds + millis_from_microseconds);

    GglKV timestamp_kv = { .key = GGL_STR("timestamp"), .val = timestamp };
    found_args[found_args_index] = timestamp_kv;

    // TODO: add deployment id and remove from bus server

    GglMap call_args = {
        .len = (size_t) found_args_index + 1,
        .pairs = found_args,
    };

    GglObject call_resp;
    GglError ret = ggl_call(
        GGL_STR("/aws/ggl/ggdeploymentd"),
        GGL_STR("create_local_deployment"),
        call_args,
        NULL,
        alloc,
        &call_resp
    );

    if (ret != GGL_ERR_OK) {
        GGL_LOGE("CreateLocalDeployment", "Failed to create local deployment.");
        return ret;
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GGL_STR("aws.greengrass#CreateLocalDeployment"),
        GGL_OBJ_MAP()
    );
}
