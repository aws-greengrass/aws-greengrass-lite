/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_SOCKET_SERVER_H
#define GGL_SOCKET_SERVER_H

/*! Common library for server listening on an unix socket */

#include <ggl/alloc.h>
#include <ggl/error.h>
#include <ggl/object.h>
#include <stdbool.h>
#include <stdint.h>

/** Pool of memory for one or more servers to use for clients.
 * `fds` and `generations` should be set to arrays of length `max_clients`. */
typedef struct {
    size_t max_clients;
    int32_t *fds;
    uint16_t *generations;
    void (*on_register)(uint32_t handle, size_t index);
    void (*on_release)(uint32_t handle, size_t index);
} SocketServerClientPool;

/** Initialize the memory of a `SocketServerClientPool`.
 * Fields should already be set before calling this. */
void ggl_socket_server_pool_init(SocketServerClientPool *client_pool);

/** Run a server listening on `socket_path`.
 * `client_ready` will be called when more data is available or if the client
 * closes the socket.
 * If `client_ready` returns an error, the connection will be cleaned up. */
GglError ggl_socket_server_listen(
    const char *socket_path,
    SocketServerClientPool *client_pool,
    GglError (*client_ready)(void *ctx, uint32_t handle),
    void *ctx
);

/** Read exact amount of data from a socket-server client. */
GglError ggl_socket_read(
    SocketServerClientPool *client_pool, uint32_t handle, GglBuffer buf
);
/** Write exact amount of data to a socket-server client. */
GglError ggl_socket_write(
    SocketServerClientPool *client_pool, uint32_t handle, GglBuffer buf
);
/** Close a socket-server client. */
GglError ggl_socket_close(SocketServerClientPool *client_pool, uint32_t handle);
/** Runs action with index, with state mutex held. */
GglError ggl_socket_with_index(
    void (*action)(void *ctx, size_t index),
    void *ctx,
    SocketServerClientPool *client_pool,
    uint32_t handle
);

#endif
