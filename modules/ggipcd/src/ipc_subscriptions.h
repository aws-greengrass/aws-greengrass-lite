// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_IPC_SUBSCRIPTIONS_H
#define GGL_IPC_SUBSCRIPTIONS_H

#include <gg/arena.h>
#include <gg/error.h>
#include <gg/types.h>
#include <stdint.h>

/// Callback for whenever a subscription is closed.
typedef GgError (*GglIpcSubscribeCallback)(
    GgObject data, uint32_t resp_handle, int32_t stream_id, GgArena *arena
);

/// Wrapper around ggl_subscribe for IPC handlers.
GgError ggl_ipc_bind_subscription(
    uint32_t resp_handle,
    int32_t stream_id,
    GgBuffer interface,
    GgBuffer method,
    GgMap params,
    GglIpcSubscribeCallback on_response,
    GgError *error
);

/// Clean up subscriptions for an IPC client
GgError ggl_ipc_release_subscriptions_for_conn(uint32_t resp_handle);

/// Cleans up subscription associated with an IPC client's stream
void ggl_ipc_terminate_stream(uint32_t resp_handle, int32_t stream_id);

#endif
