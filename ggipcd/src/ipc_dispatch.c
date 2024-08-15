// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ipc_dispatch.h"
#include "handlers/handlers.h"
#include "ipc_server.h"
#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <stdint.h>

static const struct {
    GglBuffer operation;
    GglIpcHandler *handler;
} HANDLER_TABLE[] = {
    { GGL_STR("aws.greengrass#PublishToIoTCore"), handle_publish_to_iot_core },
    { GGL_STR("aws.greengrass#SubscribeToIoTCore"),
      handle_subscribe_to_iot_core },
    { GGL_STR("aws.greengrass#PublishToTopic"), handle_publish_to_topic },
    { GGL_STR("aws.greengrass#SubscribeToTopic"), handle_subscribe_to_topic },
    { GGL_STR("aws.greengrass#CreateLocalDeployment"),
      handle_create_local_deployment },
    { GGL_STR("aws.greengrass#GetConfiguration"), handle_get_configuration },
    { GGL_STR("aws.greengrass#UpdateConfiguration"),
      handle_update_configuration }
};

static const size_t HANDLER_COUNT
    = sizeof(HANDLER_TABLE) / sizeof(HANDLER_TABLE[0]);

GglError ggl_ipc_handle_operation(
    GglBuffer operation, GglMap args, uint32_t handle, int32_t stream_id
) {
    for (size_t i = 0; i < HANDLER_COUNT; i++) {
        if (ggl_buffer_eq(operation, HANDLER_TABLE[i].operation)) {
            static uint8_t resp_mem
                [(GGL_IPC_PAYLOAD_MAX_SUBOBJECTS * sizeof(GglObject))
                 + GGL_IPC_MAX_MSG_LEN];
            GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(resp_mem));

            return HANDLER_TABLE[i].handler(
                args, handle, stream_id, &balloc.alloc
            );
        }
    }

    GGL_LOGW("ipc-server", "Unhandled operation requested.");
    return GGL_ERR_NOENTRY;
}
