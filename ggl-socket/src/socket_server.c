/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ggl/socket_server.h"
#include "ggl/socket.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <ggl/alloc.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <signal.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static GglError add_epoll_watch(int epoll_fd, int target_fd, uint64_t data) {
    assert(epoll_fd >= 0);
    assert(target_fd >= 0);

    struct epoll_event event = { .events = EPOLLIN, .data = { .u64 = data } };

    int err = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, target_fd, &event);
    if (err == -1) {
        err = errno;
        GGL_LOGE(
            "socket-server",
            "Failed to add epoll watch for %d: %d.",
            target_fd,
            err
        );
        return GGL_ERR_FAILURE;
    }
    return GGL_ERR_OK;
}

static void new_client_available(
    GglSocketPool *pool, int epoll_fd, int socket_fd
) {
    assert(epoll_fd >= 0);
    assert(socket_fd >= 0);

    int client_fd = accept(socket_fd, NULL, NULL);
    if (client_fd == -1) {
        int err = errno;
        GGL_LOGE(
            "socket-server",
            "Failed to accept on socket %d: %d.",
            socket_fd,
            err
        );
        return;
    }

    GGL_LOGD("socket-server", "Accepted new client %d.", client_fd);

    fcntl(client_fd, F_SETFD, FD_CLOEXEC);

    // To prevent deadlocking on hanged client, add a timeout
    struct timeval timeout = { .tv_sec = 5 };
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    uint32_t handle = 0;
    GglError ret = ggl_socket_pool_register(pool, client_fd, &handle);
    if (ret != GGL_ERR_OK) {
        close(client_fd);
        GGL_LOGW(
            "socket-server",
            "Closed new client %d due to max clients reached.",
            client_fd
        );
        return;
    }

    ret = add_epoll_watch(epoll_fd, client_fd, handle);
    if (ret != GGL_ERR_OK) {
        ggl_socket_close(pool, handle);
        GGL_LOGE(
            "socket-server",
            "Failed to register client %d with epoll.",
            client_fd
        );
        return;
    }
}

static void client_data_ready(
    GglSocketPool *pool,
    uint32_t handle,
    GglError (*client_ready)(void *ctx, uint32_t handle),
    void *ctx
) {
    assert(client_ready != NULL);

    GglError ret = client_ready(ctx, handle);
    if (ret != GGL_ERR_OK) {
        ggl_socket_close(pool, handle);
    }
}

static GglError create_parent_dirs(char *path) {
    char *start = path;
    for (char *end = path; *end != '\0'; end = &end[1]) {
        if (*end == '/') {
            if ((strncmp(start, "", (size_t) (end - start)) != 0)
                && (strncmp(start, ".", (size_t) (end - start)) != 0)
                && (strncmp(start, "..", (size_t) (end - start)) != 0)) {
                *end = '\0';
                int ret = mkdir(path, 0755);
                *end = '/';
                if ((ret != 0) && (errno != EEXIST)) {
                    GGL_LOGE(
                        "socket-server",
                        "Failed to create parent directories of socket: %s.",
                        path
                    );
                    return GGL_ERR_FAILURE;
                }
            }
            start = end;
        }
    }
    return GGL_ERR_OK;
}

static GglError configure_server_socket(int socket_fd, const char *path) {
    assert(socket_fd >= 0);
    assert(path != NULL);

    struct sockaddr_un addr = { .sun_family = AF_UNIX, .sun_path = { 0 } };

    size_t path_len = strlen(path);

    if (path_len >= sizeof(addr.sun_path)) {
        GGL_LOGE(
            "socket-server",
            "Socket path too long (len %lu, max %lu).",
            path_len,
            sizeof(addr.sun_path) - 1
        );
        return GGL_ERR_FAILURE;
    }

    memcpy(addr.sun_path, path, path_len);

    GglError ret = create_parent_dirs(addr.sun_path);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if ((unlink(path) == -1) && (errno != ENOENT)) {
        int err = errno;
        GGL_LOGE("socket-server", "Failed to unlink server socket: %d.", err);
        return GGL_ERR_FAILURE;
    }

    if (bind(socket_fd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
        int err = errno;
        GGL_LOGE("socket-server", "Failed to bind server socket: %d.", err);
        return GGL_ERR_FAILURE;
    }

    static const int MAX_SOCKET_BACKLOG = 10;
    if (listen(socket_fd, MAX_SOCKET_BACKLOG) == -1) {
        int err = errno;
        GGL_LOGE(
            "socket-server", "Failed to listen on server socket: %d.", err
        );
        return GGL_ERR_FAILURE;
    }

    return GGL_ERR_OK;
}

GglError ggl_socket_server_listen(
    const char *path,
    GglSocketPool *pool,
    GglError (*client_ready)(void *ctx, uint32_t handle),
    void *ctx
) {
    assert(path != NULL);
    assert(pool != NULL);
    assert(pool->fds != NULL);
    assert(pool->generations != NULL);
    assert(client_ready != NULL);

    // If SIGPIPE is not blocked, writing to a socket that the client has closed
    // will result in this process being killed.
    signal(SIGPIPE, SIG_IGN);

    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd == -1) {
        int err = errno;
        GGL_LOGE("socket-server", "Failed to create socket: %d.", err);
        return GGL_ERR_FAILURE;
    }
    GGL_DEFER(close, server_fd);

    fcntl(server_fd, F_SETFD, FD_CLOEXEC);

    GglError ret = configure_server_socket(server_fd, path);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd == -1) {
        int err = errno;
        GGL_LOGE("socket-server", "Failed to create epoll fd: %d.", err);
        return GGL_ERR_FAILURE;
    }
    GGL_DEFER(close, epoll_fd);

    // server_fd's data must be out of range of handle (uint32_t)
    static const uint64_t SERVER_FD_DATA = UINT64_MAX;
    ret = add_epoll_watch(epoll_fd, server_fd, SERVER_FD_DATA);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    struct epoll_event events[10];

    while (true) {
        int ready = epoll_wait(
            epoll_fd, events, sizeof(events) / sizeof(*events), -1
        );

        if (ready == -1) {
            int err = errno;
            GGL_LOGE("socket-server", "Failed to wait on epoll: %d.", err);
            return GGL_ERR_FAILURE;
        }

        for (int i = 0; i < ready; i++) {
            uint64_t event_data = events[i].data.u64;
            if (event_data == SERVER_FD_DATA) {
                new_client_available(pool, epoll_fd, server_fd);
            } else if (event_data <= UINT32_MAX) {
                client_data_ready(
                    pool, (uint32_t) event_data, client_ready, ctx
                );
            } else {
                GGL_LOGE("socket-server", "Invalid data returned from epoll.");
                return GGL_ERR_FAILURE;
            }
        }
    }

    return GGL_ERR_FAILURE;
}
