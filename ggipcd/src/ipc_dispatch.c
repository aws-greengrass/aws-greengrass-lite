// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ipc_dispatch.h"
#include "handlers/handlers.h"
#include "ipc_server.h"
#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <pthread.h>
#include <stdint.h>

uint8_t ggl_ipc_handler_resp_mem
    [(GGL_IPC_PAYLOAD_MAX_SUBOBJECTS * sizeof(GglObject))
     + GGL_IPC_MAX_MSG_LEN];
pthread_mutex_t ggl_ipc_handler_resp_mem_mtx = PTHREAD_MUTEX_INITIALIZER;

GglError ggl_ipc_handle_operation(
    GglBuffer operation, GglMap args, uint32_t handle, int32_t stream_id
) {
    struct {
        GglBuffer operation;
        GglIpcHandler *handler;
    } handler_table[] = { { GGL_STR("aws.greengrass#PublishToIoTCore"),
                            handle_publish_to_iot_core },
                          { GGL_STR("aws.greengrass#SubscribeToIoTCore"),
                            handle_subscribe_to_iot_core },
                          { GGL_STR("aws.greengrass#UpdateConfiguration"),
                            handle_update_configuration } };

    size_t handler_count = sizeof(handler_table) / sizeof(handler_table[0]);

    for (size_t i = 0; i < handler_count; i++) {
        if (ggl_buffer_eq(operation, handler_table[i].operation)) {
            pthread_mutex_lock(&ggl_ipc_handler_resp_mem_mtx);
            GGL_DEFER(pthread_mutex_unlock, ggl_ipc_handler_resp_mem_mtx);
            GglBumpAlloc balloc
                = ggl_bump_alloc_init(GGL_BUF(ggl_ipc_handler_resp_mem));

            return handler_table[i].handler(
                args, handle, stream_id, &balloc.alloc
            );
        }
    }

    GGL_LOGW("ipc-server", "Unhandled operation requested.");
    return GGL_ERR_NOENTRY;
}

void ggl_ipc_subscription_on_close(void *ctx, uint32_t handle) {
    GglIpcSubscriptionCtx *sub_ctx = ctx;
    (void) handle;
    ggl_ipc_release_subscription_ctx(sub_ctx);
}
