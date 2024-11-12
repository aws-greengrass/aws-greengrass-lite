// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_SOCKET_SERVER_H
#define GGL_SOCKET_SERVER_H

//! Event driven server listening on a unix socket

#include "socket_handle.h"
#include <sys/types.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <stdint.h>

/// Run a server listening on `path`.
/// `client_ready` will be called when more data is available or if the client
/// closes the socket.
/// If `client_ready` returns an error, the connection will be cleaned up.
GglError ggl_socket_server_listen(
    GglBuffer path,
    mode_t mode,
    GglSocketPool *pool,
    GglError (*client_ready)(void *ctx, uint32_t handle),
    void *ctx
);

extern void (*ggl_socket_server_ext_handler)(void);
extern int ggl_socket_server_ext_fd;

#endif
