// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/socket_epoll.h"
#include <assert.h>
#include <errno.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <sys/epoll.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

GglError ggl_socket_epoll_create(int *epoll_fd) {
    int fd = epoll_create1(EPOLL_CLOEXEC);
    if (fd == -1) {
        int err = errno;
        GGL_LOGE("Failed to create epoll fd: %d.", err);
        return GGL_ERR_FAILURE;
    }
    *epoll_fd = fd;
    return GGL_ERR_OK;
}

GglError ggl_socket_epoll_add(int epoll_fd, int target_fd, uint64_t data) {
    assert(epoll_fd >= 0);
    assert(target_fd >= 0);

    struct epoll_event event = { .events = EPOLLIN, .data = { .u64 = data } };

    int err = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, target_fd, &event);
    if (err == -1) {
        err = errno;
        GGL_LOGE("Failed to add watch for %d: %d.", target_fd, err);
        return GGL_ERR_FAILURE;
    }
    return GGL_ERR_OK;
}

GglError ggl_socket_epoll_run(
    int epoll_fd, GglError (*fd_ready)(void *ctx, uint64_t data), void *ctx
) {
    assert(epoll_fd >= 0);
    assert(fd_ready != NULL);

    struct epoll_event events[10];

    while (true) {
        int ready = epoll_wait(
            epoll_fd, events, sizeof(events) / sizeof(*events), -1
        );

        if (ready == -1) {
            if (errno == EINTR) {
                GGL_LOGT("epoll_wait interrupted.");
                continue;
            }
            int err = errno;
            GGL_LOGE("Failed to wait on epoll: %d.", err);
            return GGL_ERR_FAILURE;
        }

        for (int i = 0; i < ready; i++) {
            GglError ret = fd_ready(ctx, events[i].data.u64);
            if (ret != GGL_ERR_OK) {
                return ret;
            }
        }
    }

    return GGL_ERR_FAILURE;
}
