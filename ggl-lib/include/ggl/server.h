/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_COMMS_SERVER_H
#define GGL_COMMS_SERVER_H

#include "ggl/error.h"
#include "object.h"
#include <stdnoreturn.h>

/*! Pluggable RPC client interface */

typedef struct GglResponseHandle GglResponseHandle;

/** Listen on `path` and receive incoming RPC calls/notifications.
 * Messages are passed to `ggl_receive_callback`. */
noreturn void ggl_listen(GglBuffer path, void *ctx);

/** Function that receives messages from listen.
 * `handle` will be non-NULL if client is expecting a response.
 * If `handle` is non-NULL, it must be passed to `ggl_respond` at some point.
 * Defined by user of library.
 */
void ggl_receive_callback(
    void *ctx, GglBuffer method, GglMap params, GglResponseHandle *handle
);

/** Respond to a message received from listen.
 * Pass non-zero error to return an error, else value is the successful result.
 * On error the RPC protocol will send the error code if supported. It may use
 * the value as extra debugging information for the error.
 * If handle is NULL, does nothing. */
void ggl_respond(GglResponseHandle *handle, GglError error, GglObject value);

#endif
