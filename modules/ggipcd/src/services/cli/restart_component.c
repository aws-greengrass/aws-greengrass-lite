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
#include <gg/flags.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <ggl/core_bus/client.h>
#include <stdint.h>

GgError ggl_handle_restart_component(
    const GglIpcOperationInfo *info,
    GgMap args,
    uint32_t handle,
    int32_t stream_id,
    GglIpcError *ipc_error,
    GgArena *alloc
) {
    (void) stream_id;
    (void) alloc;

    GgObject *component_name_obj;
    GgError ret = gg_map_validate(
        args,
        GG_MAP_SCHEMA({ GG_STR("componentName"),
                        GG_REQUIRED,
                        GG_TYPE_BUF,
                        &component_name_obj })
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("RestartComponent received invalid arguments.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_INVALID_ARGUMENTS,
            .message = GG_STR("Invalid arguments provided.") };
        return GG_ERR_INVALID;
    }

    GgBuffer component_name = gg_obj_into_buf(*component_name_obj);

    ret = ggl_ipc_auth(info, component_name, &ggl_ipc_default_policy_matcher);
    if (ret != GG_ERR_OK) {
        GG_LOGE(
            "Component %.*s is not authorized to restart component %.*s.",
            (int) info->component.len,
            info->component.data,
            (int) component_name.len,
            component_name.data
        );
        *ipc_error = (GglIpcError) {
            .error_code = GGL_IPC_ERR_UNAUTHORIZED_ERROR,
            .message = GG_STR("Component not authorized to restart component.")
        };
        return ret;
    }

    GgObject result;
    GgError method_error;
    ret = ggl_call(
        GG_STR("gg_health"),
        GG_STR("restart_component"),
        GG_MAP(gg_kv(GG_STR("component_name"), *component_name_obj)),
        &method_error,
        alloc,
        &result
    );
    if (ret != GG_ERR_OK) {
        if (ret == GG_ERR_REMOTE) {
            GG_LOGE("Failed to restart component: %u", (unsigned) method_error);
            if (method_error == GG_ERR_NOENTRY) {
                *ipc_error = (GglIpcError
                ) { .error_code = GGL_IPC_ERR_RESOURCE_NOT_FOUND,
                    .message = GG_STR("Component not found.") };
                return method_error;
            }
        }
        return ggl_ipc_response_send(
            handle,
            stream_id,
            GG_STR("aws.greengrass#RestartComponentResponse"),
            GG_MAP(gg_kv(GG_STR("restartStatus"), gg_obj_buf(GG_STR("FAILED"))))
        );
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GG_STR("aws.greengrass#RestartComponentResponse"),
        GG_MAP(gg_kv(GG_STR("restartStatus"), gg_obj_buf(GG_STR("SUCCEEDED"))))
    );
}
