/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_SOCKET_H
#define GGL_SOCKET_H

/*! Socket function wrappers */

#include <ggl/error.h>
#include <ggl/object.h>

/** Wrapper for reading from socket.
 * Returns remaining buffer. */
GglError ggl_read(int fd, GglBuffer *buf);

/** Wrapper for reading full buffer from socket. */
GglError ggl_read_exact(int fd, GglBuffer buf);

/** Wrapper for writing to socket.
 * Returns remaining buffer. */
GglError ggl_write(int fd, GglBuffer *buf);

/** Wrapper for writing full buffer to socket. */
GglError ggl_write_exact(int fd, GglBuffer buf);

#endif
