// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_server.h"
#include "../../ipc_service.h"
#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <stdbool.h>
#include <stdint.h>

static GglIpcOperationHandler handle_get_system_config;

static GglIpcOperation operations[] = {
    {
        GGL_STR("aws.greengrass.private#GetSystemConfig"),
        handle_get_system_config,
    },
};

GglIpcService ggl_ipc_service_private = {
    .name = GGL_STR("aws.greengrass.ipc.private"),
    .operations = operations,
    .operation_count = sizeof(operations) / sizeof(*operations),
};

GglError handle_get_system_config(
    const GglIpcOperationInfo *info,
    GglMap args,
    uint32_t handle,
    int32_t stream_id,
    GglAlloc *alloc
) {
    (void) info;

    GglObject *key_obj;
    GglError ret = ggl_map_validate(
        args, GGL_MAP_SCHEMA({ GGL_STR("key"), true, GGL_TYPE_BUF, &key_obj })
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Received invalid parameters.");
        return GGL_ERR_INVALID;
    }

    GglObject read_value;
    ret = ggl_gg_config_read(
        GGL_BUF_LIST(GGL_STR("system"), key_obj->buf), alloc, &read_value
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return ggl_ipc_response_send(handle, stream_id, GGL_STR(""), read_value);
}
