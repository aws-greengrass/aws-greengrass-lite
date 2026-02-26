// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_error.h"
#include "../../ipc_server.h"
#include "../../ipc_service.h"
#include "lifecycle.h"
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/flags.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <ggl/core_bus/client.h>
#include <stddef.h>
#include <stdint.h>

GgError ggl_handle_update_state(
    const GglIpcOperationInfo *info,
    GgMap args,
    uint32_t handle,
    int32_t stream_id,
    GglIpcError *ipc_error,
    GgArena *alloc
) {
    (void) alloc;
    GgObject *state_obj;
    GgError ret = gg_map_validate(
        args,
        GG_MAP_SCHEMA(
            { GG_STR("state"), GG_REQUIRED, GG_TYPE_BUF, &state_obj },
        )
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Received invalid parameters.");
        *ipc_error = (GglIpcError) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
                                     .message
                                     = GG_STR("Received invalid parameters.") };
        return GG_ERR_INVALID;
    }
    GgBuffer state = gg_obj_into_buf(*state_obj);

    GG_LOGT(
        "state buffer: %.*s with length: %zu",
        (int) state.len,
        state.data,
        state.len
    );

    // No AuthZ required. UpdateState only affects the caller.
    GgObject component_obj = gg_obj_buf(info->component);

    ret = ggl_call(
        GG_STR("gg_health"),
        GG_STR("update_status"),
        GG_MAP(
            gg_kv(GG_STR("component_name"), component_obj),
            gg_kv(GG_STR("lifecycle_state"), *state_obj)
        ),
        NULL,
        NULL,
        NULL
    );
    if (ret != GG_ERR_OK) {
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GG_STR("Failed to update the lifecycle state.") };
        return ret;
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GG_STR("aws.greengrass#UpdateStateResponse"),
        (GgMap) { 0 }
    );
}
