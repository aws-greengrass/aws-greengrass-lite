// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_IPC_SUBSCRIPTIONS_H
#define GGL_IPC_SUBSCRIPTIONS_H

#include <ggl/alloc.h>
#include <ggl/error.h>
#include <ggl/object.h>
#include <stdint.h>

/// Callback for whenever a subscription is closed.
typedef GglError (*GglIpcSubscribeCallback)(
    GglObject data, uint32_t resp_handle, int32_t stream_id, GglAlloc *alloc
);

/// Wrapper around ggl_subscribe for IPC handlers.
GglError ggl_ipc_bind_subscription(
    uint32_t resp_handle,
    int32_t stream_id,
    GglBuffer interface,
    GglBuffer method,
    GglMap params,
    GglIpcSubscribeCallback on_response,
    GglError *error
) __attribute__((warn_unused_result));

/// Clean up subscriptions for an IPC client
GglError ggl_ipc_release_subscriptions_for_conn(uint32_t resp_handle);

#endif
