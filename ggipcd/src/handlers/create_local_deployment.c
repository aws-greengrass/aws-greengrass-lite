// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../ipc_server.h"
#include "handlers.h"
#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

GglError handle_create_local_deployment(
    GglMap args, uint32_t handle, int32_t stream_id, GglAlloc *alloc
) {
    GGL_MAP_FOREACH(pair, args) {
        if (ggl_buffer_eq(pair->key, GGL_STR("recipeDirectoryPath"))) {
            pair->key = GGL_STR("recipe_directory_path");
        } else if (ggl_buffer_eq(
                       pair->key, GGL_STR("artifactsDirectoryPath")
                   )) {
            pair->key = GGL_STR("artifacts_directory_path");
        } else if (ggl_buffer_eq(
                       pair->key, GGL_STR("rootComponentVersionsToAdd")
                   )) {
            pair->key = GGL_STR("root_component_versions_to_add");
        } else if (ggl_buffer_eq(
                       pair->key, GGL_STR("rootComponentVersionsToRemove")
                   )) {
            pair->key = GGL_STR("root_component_versions_to_remove");
        } else if (ggl_buffer_eq(
                       pair->key, GGL_STR("componentToConfiguration")
                   )) {
            pair->key = GGL_STR("component_to_configuration");
        } else {
            GGL_LOGE(
                "CreateLocalDeployment",
                "Unhandled argument: %.*s",
                (int) pair->key.len,
                pair->key.data
            );
        }
    }

    GglObject result;
    GglError ret = ggl_call(
        GGL_STR("/aws/ggl/ggdeploymentd"),
        GGL_STR("create_local_deployment"),
        args,
        NULL,
        alloc,
        &result
    );

    if (ret != GGL_ERR_OK) {
        GGL_LOGE("CreateLocalDeployment", "Failed to create local deployment.");
        return ret;
    }

    if (result.type != GGL_TYPE_MAP) {
        GGL_LOGE("CreateLocalDeployment", "Response not a map.");
        return GGL_ERR_FAILURE;
    }

    GglObject *val = NULL;
    bool found = ggl_map_get(result.map, GGL_STR("deployment_id"), &val);
    if (!found) {
        GGL_LOGE("CreateLocalDeployment", "Response missing deployment_id.");
        return GGL_ERR_FAILURE;
    }
    if (val->type != GGL_TYPE_BUF) {
        GGL_LOGE(
            "CreateLocalDeployment", "Response deployment_id not a string."
        );
        return GGL_ERR_FAILURE;
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GGL_STR("aws.greengrass#CreateLocalDeploymentResponse"),
        GGL_OBJ_MAP({ GGL_STR("deploymentId"), GGL_OBJ(val->buf) })
    );
}
