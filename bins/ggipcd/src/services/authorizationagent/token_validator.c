// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_components.h"
#include "../../ipc_server.h"
#include "../../ipc_service.h"
#include "authorization_agent.h"
#include "stdbool.h"
#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <stdint.h>

GglError ggl_handle_token_validation(
    const GglIpcOperationInfo *info,
    GglMap args,
    uint32_t handle,
    int32_t stream_id,
    GglAlloc *alloc
) {
    (void) alloc;
    if (!ggl_buffer_eq(
            info->component, GGL_STR("aws.greengrass.StreamManager")
        )) {
        GGL_LOGE(
            "Component %.*s does not have access to token verification IPC "
            "command",
            (int) info->component.len,
            info->component.data
        );
    }
    GglObject *svcuid_obj;
    GglError ret = ggl_map_validate(
        args,
        GGL_MAP_SCHEMA({ GGL_STR("token"), true, GGL_TYPE_BUF, &svcuid_obj }, )
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Received invalid paramters.");
        return GGL_ERR_INVALID;
    }

    if (ipc_svcuid_exists(svcuid_obj->buf) == GGL_ERR_OK) {
        return ggl_ipc_response_send(
            handle,
            stream_id,
            GGL_STR("aws.greengrass#ValidateAuthorizationTokenResponse"),
            GGL_OBJ_MAP(GGL_MAP({ GGL_STR("isValid"), GGL_OBJ_BOOL(true) }))
        );
    }
    return ggl_ipc_response_send(
        handle,
        stream_id,
        GGL_STR("aws.greengrass#ValidateAuthorizationTokenResponse"),
        GGL_OBJ_MAP(GGL_MAP({ GGL_STR("isValid"), GGL_OBJ_BOOL(false) }))
    );
}
