// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_authz.h"
#include "../../ipc_server.h"
#include "../../ipc_service.h"
#include "cli.h"
#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/ipc/common.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <stdint.h>
#include <stdlib.h>

GglError ggl_handle_create_local_deployment(
    const GglIpcOperationInfo *info,
    GglMap args,
    uint32_t handle,
    int32_t stream_id,
    GglIpcError *ipc_error,
    GglAlloc *alloc
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
                "Unhandled argument: %.*s", (int) pair->key.len, pair->key.data
            );
        }
    }

    GglError ret
        = ggl_ipc_auth(info, GGL_STR(""), ggl_ipc_default_policy_matcher);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("IPC Operation not authorized.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GGL_STR("IPC Operation not authorized.") };
        return GGL_ERR_INVALID;
    }

    GglObject result;
    ret = ggl_call(
        GGL_STR("gg_deployment"),
        GGL_STR("create_local_deployment"),
        args,
        NULL,
        alloc,
        &result
    );

    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to create local deployment.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GGL_STR("Failed to create local deployment.") };
        return ret;
    }

    if (ggl_obj_type(result) != GGL_TYPE_BUF) {
        GGL_LOGE("Received deployment ID not a string.");
        *ipc_error = (GglIpcError) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
                                     .message = GGL_STR("Internal error.") };
        return GGL_ERR_FAILURE;
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GGL_STR("aws.greengrass#CreateLocalDeploymentResponse"),
        ggl_obj_map(GGL_MAP({ GGL_STR("deploymentId"), result }))
    );
}
