// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_COREBUS_CLIENT_H
#define GGL_COREBUS_CLIENT_H

//! Core Bus client interface

#include <ggl/alloc.h>
#include <ggl/error.h>
#include <ggl/object.h>
#include <stdint.h>

/// Send a Core Bus notification (call, but don't wait for response).
GglError ggl_notify(GglBuffer interface, GglBuffer method, GglMap params);

/// Make a Core Bus call.
/// `result` will use memory from `alloc` if needed.
GglError ggl_call(
    GglBuffer interface,
    GglBuffer method,
    GglMap params,
    GglError *error,
    GglAlloc *alloc,
    GglObject *result
) __attribute__((warn_unused_result));

/// Callback for new data on a subscription.
typedef GglError (*GglSubscribeCallback)(
    void *ctx, uint32_t handle, GglObject data
);

/// Callback for whenever a subscription is closed.
typedef void (*GglSubscribeCloseCallback)(void *ctx, uint32_t handle);

/// Make an Core Bus subscription to a stream of objects.
GglError ggl_subscribe(
    GglBuffer interface,
    GglBuffer method,
    GglMap params,
    GglSubscribeCallback on_response,
    GglSubscribeCloseCallback on_close,
    void *ctx,
    GglError *error,
    uint32_t *handle
) __attribute__((warn_unused_result));

/// Close a client subscription handle.
void ggl_client_sub_close(uint32_t handle);

#endif
