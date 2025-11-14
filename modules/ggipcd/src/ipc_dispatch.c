// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ipc_dispatch.h"
#include "ipc_error.h"
#include "ipc_server.h"
#include "ipc_service.h"
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/log.h>
#include <gg/object.h>
#include <stddef.h>
#include <stdint.h>

static const GglIpcService *const SERVICE_TABLE[]
    = { &ggl_ipc_service_pubsub,          &ggl_ipc_service_mqttproxy,
        &ggl_ipc_service_config,          &ggl_ipc_service_cli,
        &ggl_ipc_service_private,         &ggl_ipc_service_lifecycle,
        &ggl_ipc_service_token_validation };

static const size_t SERVICE_COUNT
    = sizeof(SERVICE_TABLE) / sizeof(SERVICE_TABLE[0]);

GgError ggl_ipc_handle_operation(
    GgBuffer operation,
    GgMap args,
    uint32_t handle,
    int32_t stream_id,
    GglIpcError *ipc_error
) {
    for (size_t i = 0; i < SERVICE_COUNT; i++) {
        const GglIpcService *service = SERVICE_TABLE[i];
        GG_LOGT(
            "Matching against service: %.*s.",
            (int) service->name.len,
            service->name.data
        );

        for (size_t j = 0; j < service->operation_count; j++) {
            const GglIpcOperation *service_op = &service->operations[j];

            GG_LOGT(
                "Matching against operation: %.*s.",
                (int) service_op->name.len,
                service_op->name.data
            );

            if (gg_buffer_eq(operation, service_op->name)) {
                GglIpcOperationInfo info = {
                    .service = service->name,
                    .operation = operation,
                };
                GgError ret
                    = ggl_ipc_get_component_name(handle, &info.component);
                if (ret != GG_ERR_OK) {
                    GG_LOGE(
                        "Failed component name lookup for IPC operation %.*s",
                        (int) operation.len,
                        operation.data
                    );
                    return ret;
                }

                GG_LOGI(
                    "Received IPC operation %.*s from component %.*s.",
                    (int) operation.len,
                    operation.data,
                    (int) info.component.len,
                    info.component.data
                );
                static uint8_t resp_mem
                    [sizeof(GgObject[GG_MAX_OBJECT_SUBOBJECTS])
                     + GGL_IPC_MAX_MSG_LEN];
                GgArena alloc = gg_arena_init(GG_BUF(resp_mem));

                return service_op->handler(
                    &info, args, handle, stream_id, ipc_error, &alloc
                );
            }
        }
    }

    GG_LOGW(
        "Unhandled operation requested: %.*s.",
        (int) operation.len,
        operation.data
    );
    return GG_ERR_NOENTRY;
}
