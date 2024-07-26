/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_SOCKET_UTILS_H
#define GGL_SOCKET_UTILS_H

/*! Socket utils */

#include <ggl/error.h>
#include <ggl/object.h>

/** Wrapper around recv that receives full length requested. */
GglError socket_read(int fd, GglBuffer buf);

/** Wrapper around write that writes full length requested. */
GglError socket_write(int fd, GglBuffer buf);

#endif
