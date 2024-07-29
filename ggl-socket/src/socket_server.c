/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ggl/socket_server.h"
#include "ggl/socket.h"
#include "ggl/socket_epoll.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <ggl/alloc.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <stddef.h>
#include <stdint.h>

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

    ret = ggl_socket_epoll_add(epoll_fd, client_fd, handle);
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

typedef struct {
    GglSocketPool *pool;
    int epoll_fd;
    int server_fd;
    GglError (*client_ready)(void *ctx, uint32_t handle);
    void *ctx;
} SocketServerCtx;

// server_fd's data must be out of range of handle (uint32_t)
static const uint64_t SERVER_FD_DATA = UINT64_MAX;

static GglError epoll_fd_ready(void *epoll_ctx, uint64_t data) {
    SocketServerCtx *server_ctx = epoll_ctx;

    if (data == SERVER_FD_DATA) {
        new_client_available(
            server_ctx->pool, server_ctx->epoll_fd, server_ctx->server_fd
        );
    } else if (data <= UINT32_MAX) {
        client_data_ready(
            server_ctx->pool,
            (uint32_t) data,
            server_ctx->client_ready,
            server_ctx->ctx
        );
    } else {
        GGL_LOGE("socket-server", "Invalid data returned from epoll.");
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

    int epoll_fd;
    GglError ret = ggl_socket_epoll_create(&epoll_fd);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    GGL_DEFER(close, epoll_fd);

    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd == -1) {
        int err = errno;
        GGL_LOGE("socket-server", "Failed to create socket: %d.", err);
        return GGL_ERR_FAILURE;
    }
    GGL_DEFER(close, server_fd);

    fcntl(server_fd, F_SETFD, FD_CLOEXEC);

    ret = configure_server_socket(server_fd, path);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = ggl_socket_epoll_add(epoll_fd, server_fd, SERVER_FD_DATA);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    // If SIGPIPE is not blocked, writing to a socket that the client has closed
    // will result in this process being killed.
    signal(SIGPIPE, SIG_IGN);

    SocketServerCtx server_ctx = {
        .pool = pool,
        .epoll_fd = epoll_fd,
        .server_fd = server_fd,
        .client_ready = client_ready,
        .ctx = ctx,
    };

    return ggl_socket_epoll_run(epoll_fd, epoll_fd_ready, &server_ctx);
}
