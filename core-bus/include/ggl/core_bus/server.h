// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_COREBUS_SERVER_H
#define GGL_COREBUS_SERVER_H

#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/object.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

//! Core Bus server interface

/// Maximum number of core-bus connections.
/// Can be configured with `-DGGL_COREBUS_MAX_CLIENTS=<N>`.
#ifndef GGL_COREBUS_MAX_CLIENTS
#define GGL_COREBUS_MAX_CLIENTS 100
#endif

/// Function that receives client invocations of a method.
/// For call/notify, the handler must either use the handle to respond and
/// return GGL_ERR_OK, or return an error without responding. For
/// subscribe, the handler must either accept the subscription and return
/// GGL_ERR_OK, or return an error without accepting.
/// If a sub is accepted, the handle should be saved for sending responses.
typedef GglError (*GglBusHandler)(void *ctx, GglMap params, uint32_t handle);

/// Method handlers table entry for Core Bus interface.
typedef struct {
    GglBuffer name;
    bool is_subscription;
    GglBusHandler handler;
    void *ctx;
} GglRpcMethodDesc;

/// Listen on `interface` and receive incoming Core Bus method invocations.
/// If an incoming method matches a table entry, that callback is called.
GglError ggl_listen(
    GglBuffer interface, GglRpcMethodDesc *handlers, size_t handlers_len
);

/// Send a response to the client for a call/notify request.
/// Closes the connection.
/// Must be called from within a core bus handler.
void ggl_respond(uint32_t handle, GglObject value);

/// Server callback for whenever a subscription is closed.
typedef void (*GglServerSubCloseCallback)(void *ctx, uint32_t handle);

/// Accept a subscription
/// Must be called before responding on a subscription.
/// Must be called from within a core bus handler.
void ggl_sub_accept(
    uint32_t handle, GglServerSubCloseCallback on_close, void *ctx
);

/// Send a response to the client on a subscription.
/// Subscriptions must be accepted before responding.
void ggl_sub_respond(uint32_t handle, GglObject value);

/// Close a server subscription handle.
void ggl_server_sub_close(uint32_t handle);

#endif
