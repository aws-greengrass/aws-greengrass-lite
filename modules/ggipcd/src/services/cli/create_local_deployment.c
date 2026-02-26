// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_authz.h"
#include "../../ipc_error.h"
#include "../../ipc_server.h"
#include "../../ipc_service.h"
#include "cli.h"
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <ggl/core_bus/client.h>
#include <stdint.h>
#include <stdlib.h>

GgError ggl_handle_create_local_deployment(
    const GglIpcOperationInfo *info,
    GgMap args,
    uint32_t handle,
    int32_t stream_id,
    GglIpcError *ipc_error,
    GgArena *alloc
) {
    GG_MAP_FOREACH (pair, args) {
        if (gg_buffer_eq(gg_kv_key(*pair), GG_STR("recipeDirectoryPath"))) {
            gg_kv_set_key(pair, GG_STR("recipe_directory_path"));
        } else if (gg_buffer_eq(
                       gg_kv_key(*pair), GG_STR("artifactsDirectoryPath")
                   )) {
            gg_kv_set_key(pair, GG_STR("artifacts_directory_path"));
        } else if (gg_buffer_eq(
                       gg_kv_key(*pair), GG_STR("rootComponentVersionsToAdd")
                   )) {
            gg_kv_set_key(pair, GG_STR("root_component_versions_to_add"));
        } else if (gg_buffer_eq(
                       gg_kv_key(*pair), GG_STR("rootComponentVersionsToRemove")
                   )) {
            gg_kv_set_key(pair, GG_STR("root_component_versions_to_remove"));
        } else if (gg_buffer_eq(
                       gg_kv_key(*pair), GG_STR("componentToConfiguration")
                   )) {
            gg_kv_set_key(pair, GG_STR("component_to_configuration"));
        } else if (gg_buffer_eq(gg_kv_key(*pair), GG_STR("groupName"))) {
            gg_kv_set_key(pair, GG_STR("group_name"));
        } else {
            GG_LOGE(
                "Unhandled argument: %.*s",
                (int) gg_kv_key(*pair).len,
                gg_kv_key(*pair).data
            );
        }
    }

    GgError ret
        = ggl_ipc_auth(info, GG_STR(""), ggl_ipc_default_policy_matcher);
    if (ret != GG_ERR_OK) {
        GG_LOGE("IPC Operation not authorized.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GG_STR("IPC Operation not authorized.") };
        return GG_ERR_INVALID;
    }

    GgObject result;
    ret = ggl_call(
        GG_STR("gg_deployment"),
        GG_STR("create_local_deployment"),
        args,
        NULL,
        alloc,
        &result
    );

    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to create local deployment.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GG_STR("Failed to create local deployment.") };
        return ret;
    }

    if (gg_obj_type(result) != GG_TYPE_BUF) {
        GG_LOGE("Received deployment ID not a string.");
        *ipc_error = (GglIpcError) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
                                     .message = GG_STR("Internal error.") };
        return GG_ERR_FAILURE;
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GG_STR("aws.greengrass#CreateLocalDeploymentResponse"),
        GG_MAP(gg_kv(GG_STR("deploymentId"), result))
    );
}
