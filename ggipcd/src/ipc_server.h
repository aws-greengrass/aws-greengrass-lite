// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_IPC_SERVER_H
#define GGL_IPC_SERVER_H

#include <ggl/error.h>
#include <ggl/object.h>
#include <stdint.h>

/// Maximum size of eventstream packet.
/// Can be configured with `-DGGL_IPC_MAX_MSG_LEN=<N>`.
#ifndef GGL_IPC_MAX_MSG_LEN
#define GGL_IPC_MAX_MSG_LEN 10000
#endif

#define GGL_IPC_PAYLOAD_MAX_SUBOBJECTS 50

GglError ggl_ipc_listen(const char *socket_path);

GglError ggl_ipc_response_send(
    uint32_t handle,
    int32_t stream_id,
    GglBuffer service_model_type,
    GglObject response
);

typedef struct {
    uint32_t resp_handle;
    int32_t stream_id;
    uint32_t recv_handle;
} GglIpcSubscriptionCtx;

/// Claim a subscription context
GglError ggl_ipc_get_subscription_ctx(
    GglIpcSubscriptionCtx **ctx, uint32_t resp_handle
);

/// Cleanup for subscription context, for ggl_subscribe on_close
void ggl_ipc_release_subscription_ctx(GglIpcSubscriptionCtx *ctx);

/// Set the recv handle for a subscription context
GglError ggl_ipc_subscription_ctx_set_recv_handle(
    GglIpcSubscriptionCtx *ctx, uint32_t resp_handle, uint32_t recv_handle
);

#endif
