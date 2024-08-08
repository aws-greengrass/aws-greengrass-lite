// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_IPC_HANDLERS_H
#define GGL_IPC_HANDLERS_H

#include "../ipc_server.h"
#include <ggl/alloc.h>
#include <ggl/error.h>
#include <ggl/object.h>
#include <pthread.h>

typedef GglError GglIpcHandler(
    GglMap args, uint32_t handle, int32_t stream_id, GglAlloc *alloc
);

/// Memory available for core bus subscription callbacks, while mtx held
/// Passed as alloc to handlers
extern uint8_t ggl_ipc_handler_resp_mem
    [(GGL_IPC_PAYLOAD_MAX_SUBOBJECTS * sizeof(GglObject))
     + GGL_IPC_MAX_MSG_LEN];
extern pthread_mutex_t ggl_ipc_handler_resp_mem_mtx;

/// On close handler for core bus subscription callbacks
void ggl_ipc_subscription_on_close(void *ctx, uint32_t handle);

GglIpcHandler handle_publish_to_iot_core;
GglIpcHandler handle_subscribe_to_iot_core;

#endif
