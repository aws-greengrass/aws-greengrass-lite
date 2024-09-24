// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ipc_dispatch.h"
#include "ipc_server.h"
#include "ipc_service.h"
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <stddef.h>
#include <stdint.h>

static const GglIpcService *const SERVICE_TABLE[] = {
    &ggl_ipc_service_pubsub,
    &ggl_ipc_service_mqttproxy,
    &ggl_ipc_service_config,
    &ggl_ipc_service_cli,
};

static const size_t SERVICE_COUNT
    = sizeof(SERVICE_TABLE) / sizeof(SERVICE_TABLE[0]);

GglError ggl_ipc_handle_operation(
    GglBuffer operation, GglMap args, uint32_t handle, int32_t stream_id
) {
    for (size_t i = 0; i < SERVICE_COUNT; i++) {
        const GglIpcService *service = SERVICE_TABLE[i];

        for (size_t j = 0; j < service->operation_count; j++) {
            const GglIpcOperation *service_op = &service->operations[j];

            if (ggl_buffer_eq(operation, service_op->name)) {
                GglIpcOperationInfo info = {
                    .service = service->name,
                    .operation = operation,
                };
                GglError ret
                    = ggl_ipc_get_component_name(handle, &info.component);
                if (ret != GGL_ERR_OK) {
                    return ret;
                }

                static uint8_t resp_mem
                    [(GGL_IPC_PAYLOAD_MAX_SUBOBJECTS * sizeof(GglObject))
                     + GGL_IPC_MAX_MSG_LEN];
                GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(resp_mem));

                return service_op->handler(
                    &info, args, handle, stream_id, &balloc.alloc
                );
            }
        }
    }

    GGL_LOGW("ipc-server", "Unhandled operation requested.");
    return GGL_ERR_NOENTRY;
}
