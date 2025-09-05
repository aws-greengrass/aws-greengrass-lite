// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_authz.h"
#include "../../ipc_error.h"
#include "../../ipc_server.h"
#include "../../ipc_service.h"
#include "cli.h"
#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/flags.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <stdint.h>

GglError ggl_handle_restart_component(
    const GglIpcOperationInfo *info,
    GglMap args,
    uint32_t handle,
    int32_t stream_id,
    GglIpcError *ipc_error,
    GglArena *alloc
) {
    (void) stream_id;
    (void) alloc;

    GglObject *component_name_obj;
    GglError ret = ggl_map_validate(
        args,
        GGL_MAP_SCHEMA({ GGL_STR("componentName"),
                         GGL_REQUIRED,
                         GGL_TYPE_BUF,
                         &component_name_obj })
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("RestartComponent received invalid arguments.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_INVALID_ARGUMENTS,
            .message = GGL_STR("Invalid arguments provided.") };
        return GGL_ERR_INVALID;
    }

    GglBuffer component_name = ggl_obj_into_buf(*component_name_obj);

    ret = ggl_ipc_auth(info, component_name, &ggl_ipc_default_policy_matcher);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "Component %.*s is not authorized to restart component %.*s.",
            (int) info->component.len,
            info->component.data,
            (int) component_name.len,
            component_name.data
        );
        *ipc_error = (GglIpcError) {
            .error_code = GGL_IPC_ERR_UNAUTHORIZED_ERROR,
            .message = GGL_STR("Component not authorized to restart component.")
        };
        return ret;
    }

    GglObject result;
    GglError method_error;
    ret = ggl_call(
        GGL_STR("gg_health"),
        GGL_STR("restart_component"),
        GGL_MAP(ggl_kv(GGL_STR("component_name"), *component_name_obj)),
        &method_error,
        alloc,
        &result
    );
    if (ret != GGL_ERR_OK) {
        if (ret == GGL_ERR_REMOTE) {
            GGL_LOGE(
                "Failed to restart component: %u", (unsigned) method_error
            );
            if (method_error == GGL_ERR_NOENTRY) {
                *ipc_error = (GglIpcError
                ) { .error_code = GGL_IPC_ERR_RESOURCE_NOT_FOUND,
                    .message = GGL_STR("Component not found.") };
                return method_error;
            }
        }
        return ggl_ipc_response_send(
            handle,
            stream_id,
            GGL_STR("aws.greengrass#RestartComponentResponse"),
            GGL_MAP(
                ggl_kv(GGL_STR("restartStatus"), ggl_obj_buf(GGL_STR("FAILED")))
            )
        );
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GGL_STR("aws.greengrass#RestartComponentResponse"),
        GGL_MAP(
            ggl_kv(GGL_STR("restartStatus"), ggl_obj_buf(GGL_STR("SUCCEEDED")))
        )
    );
}
