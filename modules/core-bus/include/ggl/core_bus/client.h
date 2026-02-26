// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_COREBUS_CLIENT_H
#define GGL_COREBUS_CLIENT_H

//! Core Bus client interface

#include <gg/arena.h>
#include <gg/error.h>
#include <gg/types.h>
#include <stdint.h>

/// Maximum number of core-bus connections.
/// Can be configured with `-DGGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS=<N>`.
#ifndef GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS
#define GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS 100
#endif

/// Send a Core Bus notification (call, but don't wait for response).
GgError ggl_notify(GgBuffer interface, GgBuffer method, GgMap params);

/// Make a Core Bus call.
/// `result` will use memory from `alloc` if needed.
GgError ggl_call(
    GgBuffer interface,
    GgBuffer method,
    GgMap params,
    GgError *error,
    GgArena *alloc,
    GgObject *result
);

/// Callback for new data on a subscription.
typedef GgError (*GglSubscribeCallback)(
    void *ctx, uint32_t handle, GgObject data
);

/// Callback for whenever a subscription is closed.
typedef void (*GglSubscribeCloseCallback)(void *ctx, uint32_t handle);

/// Make an Core Bus subscription to a stream of objects.
GgError ggl_subscribe(
    GgBuffer interface,
    GgBuffer method,
    GgMap params,
    GglSubscribeCallback on_response,
    GglSubscribeCloseCallback on_close,
    void *ctx,
    GgError *error,
    uint32_t *handle
);

/// Close a client subscription handle.
void ggl_client_sub_close(uint32_t handle);

/// Cleanup function for closing client subscription handles.
static inline void cleanup_ggl_client_sub_close(const uint32_t *handle) {
    if (*handle != 0) {
        ggl_client_sub_close(*handle);
    }
}

#endif
