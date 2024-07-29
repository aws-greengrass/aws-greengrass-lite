/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_SOCKET_EPOLL_H
#define GGL_SOCKET_EPOLL_H

/*! Epoll wrappers */

#include <ggl/error.h>
#include <stdint.h>

/** Create an epoll fd.
 * Caller is responsible for closing. */
GglError ggl_socket_epoll_create(int *epoll_fd);

/** Add an epoll watch. */
GglError ggl_socket_epoll_add(int epoll_fd, int target_fd, uint64_t data);

/** Continuously wait on epoll, calling callback when data is ready.
 * Exits only on error waiting or error from callback. */
GglError ggl_socket_epoll_run(
    int epoll_fd, GglError (*fd_ready)(void *ctx, uint64_t data), void *ctx
);

#endif
