// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_components.h"
#include "../../ipc_server.h"
#include "../../ipc_service.h"
#include "authorization_agent.h"
#include "stdbool.h"
#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/flags.h>
#include <ggl/ipc/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <stddef.h>
#include <stdint.h>

GglError ggl_handle_token_validation(
    const GglIpcOperationInfo *info,
    GglMap args,
    uint32_t handle,
    int32_t stream_id,
    GglIpcError *ipc_error,
    GglArena *alloc
) {
    (void) alloc;
    if (!ggl_buffer_eq(
            info->component, GGL_STR("aws.greengrass.StreamManager")
        )) {
        GGL_LOGE(
            "Component %.*s does not have access to token verification IPC "
            "command.",
            (int) info->component.len,
            info->component.data
        );

        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_UNAUTHORIZED_ERROR,
            .message = GGL_STR(
                "Component does not have access to token verification IPC "
                "command."
            ) };

        return GGL_ERR_FAILURE;
    }

    GglObject *svcuid_obj;
    GglError ret = ggl_map_validate(
        args,
        GGL_MAP_SCHEMA(
            { GGL_STR("token"), GGL_REQUIRED, GGL_TYPE_BUF, &svcuid_obj },
        )
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Received invalid parameters.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GGL_STR("Received invalid parameters.") };
        return GGL_ERR_INVALID;
    }

    GglSvcuid svcuid;
    ret = ggl_ipc_svcuid_from_str(ggl_obj_into_buf(*svcuid_obj), &svcuid);
    if (ret != GGL_ERR_OK) {
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_INVALID_TOKEN_ERROR,
            .message = GGL_STR(
                "Invalid token used by stream manager when trying to authorize."
            ) };
        return ret;
    }

    if (ggl_ipc_components_get_handle(svcuid, NULL) != GGL_ERR_OK) {
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_INVALID_TOKEN_ERROR,
            .message = GGL_STR(
                "Invalid token used by stream manager when trying to authorize."
            ) };

        // Greengrass Classic returns an error to the caller instead of setting
        // the value to 'false'.
        // https://github.com/aws-greengrass/aws-greengrass-nucleus/blob/b003cf0db575f546456bef69530126cf3e0b6a68/src/main/java/com/aws/greengrass/authorization/AuthorizationIPCAgent.java#L83
        return GGL_ERR_FAILURE;
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GGL_STR("aws.greengrass#ValidateAuthorizationTokenResponse"),
        ggl_obj_map(GGL_MAP({ GGL_STR("isValid"), ggl_obj_bool(true) }))
    );
}
