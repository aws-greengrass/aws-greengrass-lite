// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_server.h"
#include "../../ipc_service.h"
#include "lifecycle.h"
#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/ipc/common.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

GglError ggl_handle_update_state(
    const GglIpcOperationInfo *info,
    GglMap args,
    uint32_t handle,
    int32_t stream_id,
    GglIpcError *ipc_error,
    GglAlloc *alloc
) {
    (void) alloc;
    GglObject *state_obj;
    GglError ret = ggl_map_validate(
        args,
        GGL_MAP_SCHEMA({ GGL_STR("state"), true, GGL_TYPE_BUF, &state_obj }, )
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Received invalid parameters.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GGL_STR("Received invalid parameters.") };
        return GGL_ERR_INVALID;
    }
    GglBuffer state = ggl_obj_into_buf(*state_obj);

    GGL_LOGT(
        "state buffer: %.*s with length: %zu",
        (int) state.len,
        state.data,
        state.len
    );

    // No AuthZ required. UpdateState only affects the caller.
    GglObject component_obj = ggl_obj_buf(info->component);

    ret = ggl_call(
        GGL_STR("gg_health"),
        GGL_STR("update_status"),
        GGL_MAP(
            { GGL_STR("component_name"), component_obj },
            { GGL_STR("lifecycle_state"), *state_obj }
        ),
        NULL,
        NULL,
        NULL
    );
    if (ret != GGL_ERR_OK) {
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GGL_STR("Failed to update the lifecycle state.") };
        return ret;
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GGL_STR("aws.greengrass#UpdateStateResponse"),
        ggl_obj_map((GglMap) { 0 })
    );
}
