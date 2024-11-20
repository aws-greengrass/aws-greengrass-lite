// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_server.h"
#include "../../ipc_service.h"
#include "lifecycle.h"
#include <ggl/alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
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
    GglAlloc *alloc
) {
    (void) alloc;
    GglObject *state_obj;
    GglError ret = ggl_map_validate(
        args,
        GGL_MAP_SCHEMA({ GGL_STR("state"), true, GGL_TYPE_BUF, &state_obj }, )
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Received invalid paramters.");
        return GGL_ERR_INVALID;
    }

    GGL_LOGT(
        "state buffer: %.*s with length: %zu",
        (int) state_obj->buf.len,
        state_obj->buf.data,
        state_obj->buf.len
    );

    // No AuthZ required. UpdateState only affects the caller.
    GglObject component_obj = GGL_OBJ_BUF(info->component);

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
        return ret;
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GGL_STR("aws.greengrass#UpdateStateResponse"),
        GGL_OBJ_MAP({ 0 })
    );
}
