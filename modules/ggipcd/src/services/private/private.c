// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_error.h"
#include "../../ipc_server.h"
#include "../../ipc_service.h"
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/flags.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <ggl/core_bus/gg_config.h>
#include <stdint.h>

static GglIpcOperationHandler handle_get_system_config;

static GglIpcOperation operations[] = {
    {
        GG_STR("aws.greengrass.private#GetSystemConfig"),
        handle_get_system_config,
    },
};

GglIpcService ggl_ipc_service_private = {
    .name = GG_STR("aws.greengrass.ipc.private"),
    .operations = operations,
    .operation_count = sizeof(operations) / sizeof(*operations),
};

GgError handle_get_system_config(
    const GglIpcOperationInfo *info,
    GgMap args,
    uint32_t handle,
    int32_t stream_id,
    GglIpcError *ipc_error,
    GgArena *alloc
) {
    (void) info;

    GgObject *key_obj;
    GgError ret = gg_map_validate(
        args,
        GG_MAP_SCHEMA({ GG_STR("key"), GG_REQUIRED, GG_TYPE_BUF, &key_obj })
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Received invalid parameters.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_INVALID_ARGUMENTS,
            .message = GG_STR("Received invalid parameters.") };
        return GG_ERR_INVALID;
    }

    GgBuffer key = gg_obj_into_buf(*key_obj);
    GgObject read_value;
    ret = ggl_gg_config_read(
        GG_BUF_LIST(GG_STR("system"), key), alloc, &read_value
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to read the system configuration.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GG_STR("Failed to read the system configuration.") };
        return ret;
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GG_STR(""),
        GG_MAP(gg_kv(GG_STR("value"), read_value))
    );
}
