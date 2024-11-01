// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/socket.h"
#include <errno.h>
#include <ggl/buffer.h>
#include <ggl/cleanup.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/io.h>
#include <ggl/log.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>

GglError ggl_socket_read(int fd, GglBuffer buf) {
    GglError ret = ggl_file_read_exact(fd, buf);
    if (ret == GGL_ERR_NODATA) {
        GGL_LOGD("Socket %d closed by peer.", fd);
    }
    return ret;
}

GglError ggl_socket_write(int fd, GglBuffer buf) {
    return ggl_file_write(fd, buf);
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
    return ggl_file_read(*fd, buf);
}

GglReader ggl_socket_reader(int *fd) {
    return (GglReader) { .read = socket_reader_fn, .ctx = fd };
}
