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
#include <time.h>
#include <stdint.h>
#include <stdlib.h>

#define CREATE_LOCAL_DEPLOYMENT "CreateLocalDeployment"
#define ARTIFACT_DIRECTORY_PATH "artifactDirectoryPath"
#define RECIPE_DIRECTORY_PATH "recipeDirectoryPath"
#define ROOT_COMPONENT_VERSIONS_TO_ADD "rootComponentVersionsToAdd"
#define ROOT_COMPONENTS_TO_REMOVE "rootComponentsToRemove"
#define COMPONENT_TO_CONFIGURATION "componentToConfiguration"
#define COMPONENT_TO_RUN_WITH_INFO "componentToRunWithInfo"
#define GROUP_NAME "groupName"
#define TIMESTAMP "timestamp"

GglError handle_create_local_deployment(
    GglMap args, uint32_t handle, int32_t stream_id, GglAlloc *alloc
) {
    (void) alloc;

    GglObject *val = NULL;
    bool found = ggl_map_get(args, GGL_STR(RECIPE_DIRECTORY_PATH), &val);
    if (found && (val->type != GGL_TYPE_BUF)) {
        GGL_LOGE(
            CREATE_LOCAL_DEPLOYMENT, "%s not a string.", RECIPE_DIRECTORY_PATH
        );
        return GGL_ERR_INVALID;
    }
    GglBuffer recipe_directory_path = val->buf;

    found = ggl_map_get(args, GGL_STR(ARTIFACT_DIRECTORY_PATH), &val);
    if (found && (val->type != GGL_TYPE_BUF)) {
        GGL_LOGE(
            CREATE_LOCAL_DEPLOYMENT, "%s not a string.", ARTIFACT_DIRECTORY_PATH
        );
        return GGL_ERR_INVALID;
    }
    GglBuffer artifact_directory_path = val->buf;

    found = ggl_map_get(args, GGL_STR(ROOT_COMPONENT_VERSIONS_TO_ADD), &val);
    if (found && (val->type != GGL_TYPE_MAP)) {
        GGL_LOGE(
            CREATE_LOCAL_DEPLOYMENT,
            "%s must be provided a map.",
            ROOT_COMPONENT_VERSIONS_TO_ADD
        );
        return GGL_ERR_INVALID;
    }
    GglMap component_to_version_map = val->map;

    found = ggl_map_get(args, GGL_STR(ROOT_COMPONENTS_TO_REMOVE), &val);
    if (found && (val->type != GGL_TYPE_LIST)) {
        GGL_LOGE(
            CREATE_LOCAL_DEPLOYMENT,
            "%s must be provided a list.",
            ROOT_COMPONENTS_TO_REMOVE
        );
        return GGL_ERR_INVALID;
    }
    GglMap root_components_to_remove = val->list;

    found = ggl_map_get(args, GGL_STR(COMPONENT_TO_CONFIGURATION), &val);
    if (found && (val->type != GGL_TYPE_MAP)) {
        GGL_LOGE(
            CREATE_LOCAL_DEPLOYMENT,
            "%s must be provided a map.",
            COMPONENT_TO_CONFIGURATION
        );
        return GGL_ERR_INVALID;
    }
    GglMap component_to_configuration = val->map;

    found = ggl_map_get(args, GGL_STR(COMPONENT_TO_RUN_WITH_INFO), &val);
    if (found && (val->type != GGL_TYPE_MAP)) {
        GGL_LOGE(
            CREATE_LOCAL_DEPLOYMENT,
            "%s must be provided a map.",
            COMPONENT_TO_RUN_WITH_INFO
        );
        return GGL_ERR_INVALID;
    }
    GglMap component_to_run_with_info = val->map;

    found = ggl_map_get(args, GGL_STR(GROUP_NAME), &val);
    if (found && (val->type != GGL_TYPE_BUF)) {
        GGL_LOGE(CREATE_LOCAL_DEPLOYMENT, "%s not a string.", GROUP_NAME);
        return GGL_ERR_INVALID;
    }
    GglBuffer group_name = val->buf;

    int64_t timestamp = (int64_t) time(NULL);

    // TODO: add deployment id

    GglMap call_args = GGL_MAP(
        { GGL_STR(RECIPE_DIRECTORY_PATH), GGL_OBJ(recipe_directory_path) },
        { GGL_STR(ARTIFACT_DIRECTORY_PATH), GGL_OBJ(artifact_directory_path) },
        { GGL_STR(ROOT_COMPONENT_VERSIONS_TO_ADD),
          GGL_OBJ(component_to_version_map) },
        { GGL_STR(ROOT_COMPONENTS_TO_REMOVE),
          GGL_OBJ(root_components_to_remove) },
        { GGL_STR(COMPONENT_TO_CONFIGURATION),
          GGL_OBJ(component_to_configuration) },
        { GGL_STR(COMPONENT_TO_RUN_WITH_INFO),
          GGL_OBJ(component_to_run_with_info) },
        { GGL_STR(GROUP_NAME), GGL_OBJ(group_name) },
        { GGL_STR(TIMESTAMP), timestamp }
    );

    GglObject call_resp;
    GglError ret = ggl_call(
        GGL_STR("/aws/ggl/ggdeploymentd"),
        GGL_STR("create_local_deployment"),
        call_args,
        NULL,
        alloc,
        &call_resp
    );

    if (ret !+GGL_ERR_OK) {
        GGL_LOGE(CREATE_LOCAL_DEPLOYMENT, "Failed to create local deployment.");
        return ret;
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GGL_STR("aws.greengrass#CreateLocalDeployment"),
        GGL_OBJ_MAP()
    );
}
