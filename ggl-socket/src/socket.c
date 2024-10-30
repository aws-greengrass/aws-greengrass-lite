// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/socket.h"
#include <sys/types.h>
#include <errno.h>
#include <ggl/buffer.h>
#include <ggl/cleanup.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/io.h>
#include <ggl/log.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdint.h>

// Lowest allowed priority in order to run before threads are created.
__attribute__((constructor(101))) static void ignore_sigpipe(void) {
    // If SIGPIPE is not blocked, writing to a socket that the server has closed
    // will result in this process being killed.
    signal(SIGPIPE, SIG_IGN);
}

GglError ggl_read(int fd, GglBuffer *buf) {
    ssize_t ret = read(fd, buf->data, buf->len);
    if (ret < 0) {
        if (errno == EINTR) {
            return GGL_ERR_OK;
        }
        if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
            GGL_LOGE("recv timed out on socket %d.", fd);
            return GGL_ERR_FAILURE;
        }
        int err = errno;
        GGL_LOGE("Failed to recv on %d: %d.", fd, err);
        return GGL_ERR_FAILURE;
    }
    if (ret == 0) {
        GGL_LOGD("Socket %d closed.", fd);
        return GGL_ERR_NOCONN;
    }

    *buf = ggl_buffer_substr(*buf, (size_t) ret, SIZE_MAX);
    return GGL_ERR_OK;
}

GglError ggl_read_exact(int fd, GglBuffer buf) {
    GglBuffer rest = buf;

    while (rest.len > 0) {
        GglError ret = ggl_read(fd, &rest);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    return GGL_ERR_OK;
}

GglError ggl_write(int fd, GglBuffer *buf) {
    ssize_t ret = write(fd, buf->data, buf->len);
    if (ret < 0) {
        if (errno == EINTR) {
            return GGL_ERR_OK;
        }
        if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
            GGL_LOGE("Write timed out on socket %d.", fd);
            return GGL_ERR_FAILURE;
        }
        if (errno == EPIPE) {
            GGL_LOGE("Write failed to %d; peer closed socket.", fd);
            return GGL_ERR_NOCONN;
        }
        int err = errno;
        GGL_LOGE("Failed to write to socket %d: %d.", fd, err);
        return GGL_ERR_FAILURE;
    }

    *buf = ggl_buffer_substr(*buf, (size_t) ret, SIZE_MAX);
    return GGL_ERR_OK;
}

GglError ggl_write_exact(int fd, GglBuffer buf) {
    GglBuffer rest = buf;

    while (rest.len > 0) {
        GglError ret = ggl_write(fd, &rest);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    return GGL_ERR_OK;
}

GglError ggl_connect(GglBuffer path, int *fd) {
    struct sockaddr_un addr = { .sun_family = AF_UNIX, .sun_path = { 0 } };

    // TODO: Use symlinks to handle long paths
    if (path.len >= sizeof(addr.sun_path)) {
        GGL_LOGE("Socket path too long.");
        return GGL_ERR_FAILURE;
    }

    memcpy(addr.sun_path, path.data, path.len);

    int sockfd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (sockfd == -1) {
        GGL_LOGE("Failed to create socket: %d.", errno);
        return GGL_ERR_FATAL;
    }
    GGL_CLEANUP_ID(sockfd_cleanup, cleanup_close, sockfd);

    if (connect(sockfd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
        GGL_LOGW(
            "Failed to connect to server (%.*s): %d.",
            (int) path.len,
            path.data,
            errno
        );
        return GGL_ERR_FAILURE;
    }

    // To prevent deadlocking on hanged server, add a timeout
    struct timeval timeout = { .tv_sec = 5 };
    int sys_ret = setsockopt(
        sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)
    );
    if (sys_ret == -1) {
        GGL_LOGE("Failed to set receive timeout on socket: %d.", errno);
        return GGL_ERR_FATAL;
    }
    sys_ret = setsockopt(
        sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)
    );
    if (sys_ret == -1) {
        GGL_LOGE("Failed to set send timeout on socket: %d.", errno);
        return GGL_ERR_FATAL;
    }

    // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores) false positive
    sockfd_cleanup = -1;
    *fd = sockfd;
    return GGL_ERR_OK;
}

static GglError socket_reader_fn(void *ctx, GglBuffer *buf) {
    int *fd = ctx;

    GglBuffer rest = *buf;
    while (rest.len > 0) {
        GglError ret = ggl_read(*fd, &rest);
        if (ret != GGL_ERR_OK) {
            if (ret == GGL_ERR_NOCONN) {
                buf->len = (size_t) (rest.data - buf->data);
                return GGL_ERR_OK;
            }
            return ret;
        }
    }

    return GGL_ERR_OK;
}

GglReader ggl_socket_reader(int *fd) {
    return (GglReader) { .read = socket_reader_fn, .ctx = fd };
}
