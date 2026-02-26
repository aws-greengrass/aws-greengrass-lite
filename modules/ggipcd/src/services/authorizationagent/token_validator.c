// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_components.h"
#include "../../ipc_error.h"
#include "../../ipc_server.h"
#include "../../ipc_service.h"
#include "authorization_agent.h"
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/flags.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

GgError ggl_handle_token_validation(
    const GglIpcOperationInfo *info,
    GgMap args,
    uint32_t handle,
    int32_t stream_id,
    GglIpcError *ipc_error,
    GgArena *alloc
) {
    (void) alloc;
    if (!gg_buffer_eq(
            info->component, GG_STR("aws.greengrass.StreamManager")
        )) {
        GG_LOGE(
            "Component %.*s does not have access to token verification IPC command.",
            (int) info->component.len,
            info->component.data
        );

        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_UNAUTHORIZED_ERROR,
            .message = GG_STR(
                "Component does not have access to token verification IPC command."
            ) };

        return GG_ERR_FAILURE;
    }

    GgObject *svcuid_obj;
    GgError ret = gg_map_validate(
        args,
        GG_MAP_SCHEMA(
            { GG_STR("token"), GG_REQUIRED, GG_TYPE_BUF, &svcuid_obj },
        )
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Received invalid parameters.");
        *ipc_error = (GglIpcError) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
                                     .message
                                     = GG_STR("Received invalid parameters.") };
        return GG_ERR_INVALID;
    }

    GglSvcuid svcuid;
    ret = ggl_ipc_svcuid_from_str(gg_obj_into_buf(*svcuid_obj), &svcuid);
    if (ret != GG_ERR_OK) {
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_INVALID_TOKEN_ERROR,
            .message = GG_STR(
                "Invalid token used by stream manager when trying to authorize."
            ) };
        return ret;
    }

    if (ggl_ipc_components_get_handle(svcuid, NULL) != GG_ERR_OK) {
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_INVALID_TOKEN_ERROR,
            .message = GG_STR(
                "Invalid token used by stream manager when trying to authorize."
            ) };

        // Greengrass Classic returns an error to the caller instead of setting
        // the value to 'false'.
        // https://github.com/aws-greengrass/aws-greengrass-nucleus/blob/b003cf0db575f546456bef69530126cf3e0b6a68/src/main/java/com/aws/greengrass/authorization/AuthorizationIPCAgent.java#L83
        return GG_ERR_FAILURE;
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GG_STR("aws.greengrass#ValidateAuthorizationTokenResponse"),
        GG_MAP(gg_kv(GG_STR("isValid"), gg_obj_bool(true)))
    );
}
