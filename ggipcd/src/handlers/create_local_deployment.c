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
#include <sys/time.h>
#include <stdint.h>
#include <stdlib.h>

GglError handle_create_local_deployment(
    GglMap args, uint32_t handle, int32_t stream_id, GglAlloc *alloc
) {
    (void) alloc;

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

    GglError ret = ggl_call(
        GGL_STR("/aws/ggl/ggdeploymentd"),
        GGL_STR("create_local_deployment"),
        args,
        NULL,
        NULL,
        NULL
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
