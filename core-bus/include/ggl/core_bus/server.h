/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_COREBUS_SERVER_H
#define GGL_COREBUS_SERVER_H

#include <ggl/error.h>
#include <ggl/object.h>

/*! Core Bus server interface */

/** Opaque handle to an client awaiting a response. */
typedef uint32_t GglResponseHandle;

/** Function that receives client invocations of a method.
 * The handle must be used return an error or respond.
 * The handle may be saved to defer responding or continue use past this call.
 * Subscriptions must be accepted before responding. */
typedef void (*GglBusHandler)(
    void *ctx, GglMap params, GglResponseHandle handle
);

/** Method handlers table entry for Core Bus interface. */
typedef struct {
    GglBuffer name;
    bool is_subscription;
    GglBusHandler handler;
    void *ctx;
} GglRpcMethodDesc;

/** Listen on `interface` and receive incoming Core Bus method invocations.
 * If an incoming method matches a table entry, that callback is called. */
GglError ggl_listen(
    GglBuffer interface, GglRpcMethodDesc *handlers, size_t handlers_len
);

/** Respond with an error to a call/notify/subscribe.
 * Closes the connection.
 * For subscriptions, this may only be called if no response has been sent. */
void ggl_return_err(GglResponseHandle handle, GglError error);

/** Send a response to the client.
 * For call/notify, this closes the connection. */
void ggl_respond(GglResponseHandle handle, GglObject value);

/** Server callback for whenever a subscription is closed. */
typedef void (*GglServerSubCloseCallback)(void *ctx, GglResponseHandle handle);

/** Accept a subscription
 * Must be called before responding on a subscription. */
void ggl_sub_accept(
    GglResponseHandle handle, GglServerSubCloseCallback on_close, void *ctx
);

/** Close a server subscription handle. */
void ggl_server_sub_close(GglResponseHandle handle);

#endif
