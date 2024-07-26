/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ggl/socket_server.h"
#include <assert.h>
#include <errno.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// NOLINTNEXTLINE(readability-non-const-parameter) False positive
static GglError add_epoll_watch(int epoll_fd, int target_fd, uint64_t data) {
    assert(epoll_fd >= 0);

    struct epoll_event event = { .events = EPOLLIN, .data = { .u64 = data } };

    int err = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, target_fd, &event);
    if (err == -1) {
        err = errno;
        GGL_LOGE("socket-server", "Failed to add epoll watch: %d.", err);
        return GGL_ERR_FAILURE;
    }
    return GGL_ERR_OK;
}

static void client_ready(
    int epoll_fd,
    int socket_fd,
    bool (*register_client_fd)(void *ctx, int fd, uint32_t *token),
    bool (*release_client_fd)(void *ctx, uint32_t token, int *fd),
    void *ctx
) {
    assert(epoll_fd >= 0);
    assert(socket_fd >= 0);
    assert(register_client_fd != NULL);
    assert(release_client_fd != NULL);

    int client_fd = accept(socket_fd, NULL, NULL);
    if (client_fd == -1) {
        int err = errno;
        GGL_LOGE("socket-server", "Failed to accept on socket: %d.", err);
        return;
    }

    // To prevent deadlocking on hanged client, add a timeout
    struct timeval timeout = { .tv_sec = 4 };
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    uint32_t token = 0;
    bool registered = register_client_fd(ctx, client_fd, &token);
    if (!registered) {
        close(client_fd);
        GGL_LOGD(
            "socket-server", "Closed new client due to max clients reached."
        );
        return;
    }

    GglError ret = add_epoll_watch(epoll_fd, client_fd, token);
    if (ret != GGL_ERR_OK) {
        bool released = release_client_fd(ctx, token, &client_fd);
        if (released) {
            close(client_fd);
        }
        GGL_LOGE("socket-server", "Failed to register client fd with epoll.");
        return;
    }

    GGL_LOGD("socket-server", "Accepted client connection.");
}

static GglError client_data_ready(
    uint32_t token,
    bool (*release_client_fd)(void *ctx, uint32_t token, int *fd),
    GglError (*data_ready)(void *ctx, uint32_t token),
    void *ctx
) {
    assert(release_client_fd != NULL);
    assert(data_ready != NULL);

    GglError ret = data_ready(ctx, token);
    if (ret != GGL_ERR_OK) {
        int client_fd = -1;
        bool released = release_client_fd(ctx, token, &client_fd);
        if (released) {
            close(client_fd);
        }
    }
    return GGL_ERR_OK;
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

static GglError configure_socket(int socket_fd, const char *socket_path) {
    assert(socket_fd >= 0);
    assert(socket_path != NULL);

    struct sockaddr_un addr = { .sun_family = AF_UNIX, .sun_path = { 0 } };

    size_t path_len = strlen(socket_path);

    if (path_len >= sizeof(addr.sun_path)) {
        GGL_LOGE("socket-server", "Socket path too long.");
        return GGL_ERR_FAILURE;
    }

    memcpy(addr.sun_path, socket_path, path_len);

    GglError ret = create_parent_dirs(addr.sun_path);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if ((unlink(socket_path) == -1) && (errno != ENOENT)) {
        int err = errno;
        GGL_LOGE("socket-server", "Failed to unlink socket: %d.", err);
        return GGL_ERR_FAILURE;
    }

    if (bind(socket_fd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
        int err = errno;
        GGL_LOGE("socket-server", "Failed to bind socket: %d.", err);
        return GGL_ERR_FAILURE;
    }

    static const int MAX_SOCKET_BACKLOG = 20;
    if (listen(socket_fd, MAX_SOCKET_BACKLOG) == -1) {
        int err = errno;
        GGL_LOGE("socket-server", "Failed to listen on socket: %d.", err);
        return GGL_ERR_FAILURE;
    }

    return GGL_ERR_OK;
}

GglError ggl_socket_server_listen(
    const char *socket_path,
    bool (*register_client_fd)(void *ctx, int fd, uint32_t *token),
    bool (*release_client_fd)(void *ctx, uint32_t token, int *fd),
    GglError (*data_ready)(void *ctx, uint32_t token),
    void *ctx
) {
    assert(socket_path != NULL);
    assert(register_client_fd != NULL);
    assert(release_client_fd != NULL);
    assert(data_ready != NULL);

    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd == -1) {
        int err = errno;
        GGL_LOGE("socket-server", "Failed to create socket: %d.", err);
        return GGL_ERR_FAILURE;
    }
    GGL_DEFER(close, server_fd);

    GglError ret = configure_socket(server_fd, socket_path);
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

    // server_fd data larger than uint32_t to not conflict with caller tokens
    ret = add_epoll_watch(epoll_fd, server_fd, UINT64_MAX);
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
            if (events[i].data.u64 == UINT64_MAX) {
                client_ready(
                    epoll_fd,
                    server_fd,
                    register_client_fd,
                    release_client_fd,
                    ctx
                );
            } else if (events[i].data.u64 <= UINT32_MAX) {
                ret = client_data_ready(
                    (uint32_t) events[i].data.u64,
                    release_client_fd,
                    data_ready,
                    ctx
                );
                if (ret != GGL_ERR_OK) {
                    return ret;
                }
            } else {
                GGL_LOGE("socket-server", "Invalid data returned from epoll");
                return GGL_ERR_FAILURE;
            }
        }
    }

    return GGL_ERR_FAILURE;
}
