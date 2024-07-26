/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_SOCKET_SERVER_H
#define GGL_SOCKET_SERVER_H

/*! Common library for server listening on an unix socket */

#include <ggl/error.h>
#include <stdbool.h>
#include <stdint.h>

/** Run a server listening on `socket_path`.
 * Memory for client file descriptors is managed by the caller.
 * `get_client_fd` will be used to get memory for a file descriptor.
 * The fd can be embedded in a per-connection context to store client info.
 * `release_client_fd` should clean up any context for the given client fd.
 * The socket will be closed by the server code.
 * `data_ready` will be called when more data is available on a client fd or if
 * the client closes the socket. To terminate the connection and clean up its
 * resources, return an error from `data_ready`.
 * */
GglError ggl_socket_server_listen(
    const char *socket_path,
    bool (*register_client_fd)(void *ctx, int fd, uint32_t *token),
    bool (*release_client_fd)(void *ctx, uint32_t token, int *fd),
    GglError (*data_ready)(void *ctx, uint32_t token),
    void *ctx
);

#endif
