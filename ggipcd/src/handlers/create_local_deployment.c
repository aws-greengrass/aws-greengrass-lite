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

GglError handle_create_local_deployment(
    GglMap args, uint32_t handle, int32_t stream_id, GglAlloc *alloc
) {
    (void) alloc;

    GglObject *val = NULL;
    bool found = ggl_map_get(args, GGL_STR("recipeDirectoryPath"), &val);
    if (found && (val->type != GGL_TYPE_BUF)) {
        GGL_LOGE("CreateLocalDeployment", "recipeDirectoryPath not a string.");
        return GGL_ERR_INVALID;
    }
    GglBuffer recipe_directory_path = val->buf;

    found = ggl_map_get(args, GGL_STR("artifactDirectoryPath"), &val);
    if (found && (val->type != GGL_TYPE_BUF)) {
        GGL_LOGE(
            "CreateLocalDeployment", "artifactDirectoryPath not a string."
        );
        return GGL_ERR_INVALID;
    }
    GglBuffer artifact_directory_path = val->buf;

    found = ggl_map_get(args, GGL_STR("rootComponentVersionsToAdd"), &val);
    if (found && (val->type != GGL_TYPE_MAP)) {
        GGL_LOGE(
            "CreateLocalDeployment",
            "rootComponentVersionsToAdd must be provided a map.",
        );
        return GGL_ERR_INVALID;
    }
    GglMap component_to_version_map = val->map;

    found = ggl_map_get(args, GGL_STR("rootComponentsToRemove"), &val);
    if (found && (val->type != GGL_TYPE_LIST)) {
        GGL_LOGE(
            "CreateLocalDeployment",
            "rootComponentsToRemove must be provided a list.",
        );
        return GGL_ERR_INVALID;
    }
    GglMap root_components_to_remove = val->list;

    found = ggl_map_get(args, GGL_STR("componentToConfiguration"), &val);
    if (found && (val->type != GGL_TYPE_MAP)) {
        GGL_LOGE(
            "CreateLocalDeployment",
            "componentToConfiguration must be provided a map.",
        );
        return GGL_ERR_INVALID;
    }
    GglMap component_to_configuration = val->map;

    found = ggl_map_get(args, GGL_STR("componentToRunWithInfo"), &val);
    if (found && (val->type != GGL_TYPE_MAP)) {
        GGL_LOGE(
            "CreateLocalDeployment",
            "componentToRunWithInfo must be provided a map.",
        );
        return GGL_ERR_INVALID;
    }
    GglMap component_to_run_with_info = val->map;

    found = ggl_map_get(args, GGL_STR("groupName"), &val);
    if (found && (val->type != GGL_TYPE_BUF)) {
        GGL_LOGE("CreateLocalDeployment", "groupName not a string.");
        return GGL_ERR_INVALID;
    }
    GglBuffer group_name = val->buf;

    int64_t timestamp = (int64_t) time(NULL);

    // TODO: add deployment id

    GglMap call_args = GGL_MAP(
        { GGL_STR("recipeDirectoryPath"), GGL_OBJ(recipe_directory_path) },
        { GGL_STR("artifactDirectoryPath"), GGL_OBJ(artifact_directory_path) },
        { GGL_STR("rootComponentVersionsToAdd"),
          GGL_OBJ(component_to_version_map) },
        { GGL_STR("rootComponentsToRemove"),
          GGL_OBJ(root_components_to_remove) },
        { GGL_STR("componentToConfiguration"),
          GGL_OBJ(component_to_configuration) },
        { GGL_STR("componentToRunWithInfo"),
          GGL_OBJ(component_to_run_with_info) },
        { GGL_STR("groupName"), GGL_OBJ(group_name) },
        { GGL_STR("timestamp"), GGL_OBJ(timestamp) }
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
