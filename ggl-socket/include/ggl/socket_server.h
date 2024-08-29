// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_SOCKET_SERVER_H
#define GGL_SOCKET_SERVER_H

//! Event driven server listening on a unix socket

#include "socket_handle.h"
#include <ggl/error.h>
#include <ggl/object.h>
#include <stdint.h>

/// Run a server listening on `path`.
/// `client_ready` will be called when more data is available or if the client
/// closes the socket.
/// If `client_ready` returns an error, the connection will be cleaned up.
GglError ggl_socket_server_listen(
    GglBuffer path,
    GglSocketPool *pool,
    GglError (*client_ready)(void *ctx, uint32_t handle),
    void *ctx
);

#endif
