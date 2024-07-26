/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ggl/socket_utils.h"
#include <assert.h>
#include <errno.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

GglError socket_read(int fd, GglBuffer buf) {
    size_t read = 0;

    while (read < buf.len) {
        ssize_t ret = recv(fd, &buf.data[read], buf.len - read, MSG_WAITALL);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            int err = errno;
            GGL_LOGE("ipc-server", "Failed to recv from client: %d.", err);
            return GGL_ERR_FAILURE;
        }
        if (ret == 0) {
            GGL_LOGD("ipc-server", "Client socket closed");
            return GGL_ERR_NOCONN;
        }
        read += (size_t) ret;
    }

    assert(read == buf.len);
    return GGL_ERR_OK;
}

GglError socket_write(int fd, GglBuffer buf) {
    size_t written = 0;

    while (written < buf.len) {
        ssize_t ret = write(fd, &buf.data[written], buf.len - written);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            int err = errno;
            GGL_LOGE("ipc-server", "Failed to write to client: %d.", err);
            return GGL_ERR_FAILURE;
        }
        written += (size_t) ret;
    }

    assert(written == buf.len);
    return GGL_ERR_OK;
}
