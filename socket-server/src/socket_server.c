/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ggl/socket_server.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <pthread.h>
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

// Generational counters are used to prevent use of dangling references after
// resources for a client are cleaned up.

/** Protects client state. */
static pthread_mutex_t client_fd_mtx;

__attribute__((constructor)) static void init_client_fd_mtx(void) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&client_fd_mtx, &attr);
}

static const int32_t CLIENT_FD_FREE = -2;

void ggl_socket_server_pool_init(SocketServerClientPool *client_pool) {
    assert(client_pool != NULL);
    assert(client_pool->fds != NULL);
    assert(client_pool->generations != NULL);

    for (size_t i = 0; i < client_pool->max_clients; i++) {
        client_pool->fds[i] = CLIENT_FD_FREE;
    }
}

static bool register_client_fd(
    SocketServerClientPool *client_pool, int fd, uint32_t *handle
) {
    if (fd < 0) {
        return false;
    }

    pthread_mutex_lock(&client_fd_mtx);
    GGL_DEFER(pthread_mutex_unlock, client_fd_mtx);

    for (uint16_t i = 0; i < (uint16_t) client_pool->max_clients; i++) {
        if (client_pool->fds[i] == CLIENT_FD_FREE) {
            client_pool->fds[i] = fd;
            *handle = (uint32_t) client_pool->generations[i] << 16 | i;
            GGL_LOGD(
                "socket-server",
                "Registered fd %d at index %u, generation %u",
                fd,
                (unsigned) i,
                (unsigned) client_pool->generations[i]
            );

            if (client_pool->on_register != NULL) {
                client_pool->on_register(*handle, i);
            }
            return true;
        }
    }

    return false;
}

static bool release_client_fd(
    SocketServerClientPool *client_pool, uint32_t handle, int *fd
) {
    uint16_t index = (uint16_t) (handle & UINT16_MAX);
    uint16_t generation = (uint16_t) (handle >> 16);

    pthread_mutex_lock(&client_fd_mtx);
    GGL_DEFER(pthread_mutex_unlock, client_fd_mtx);

    if (generation != client_pool->generations[index]) {
        GGL_LOGD("socket-server", "Generation mismatch in %s.", __func__);
        return false;
    }

    *fd = client_pool->fds[index];

    client_pool->generations[index] += 1;
    client_pool->fds[index] = CLIENT_FD_FREE;
    GGL_LOGD(
        "socket-server",
        "Releasing fd %d at index %u, generation %u",
        *fd,
        (unsigned) index,
        (unsigned) generation
    );

    if (client_pool->on_release != NULL) {
        client_pool->on_release(handle, index);
    }

    return true;
}

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

static void new_client_ready(
    SocketServerClientPool *client_pool, int epoll_fd, int socket_fd
) {
    assert(epoll_fd >= 0);
    assert(socket_fd >= 0);

    int client_fd = accept(socket_fd, NULL, NULL);
    if (client_fd == -1) {
        int err = errno;
        GGL_LOGE("socket-server", "Failed to accept on socket: %d.", err);
        return;
    }

    fcntl(client_fd, F_SETFD, FD_CLOEXEC);

    // To prevent deadlocking on hanged client, add a timeout
    struct timeval timeout = { .tv_sec = 4 };
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    uint32_t handle = 0;
    bool registered = register_client_fd(client_pool, client_fd, &handle);
    if (!registered) {
        close(client_fd);
        GGL_LOGD(
            "socket-server", "Closed new client due to max clients reached."
        );
        return;
    }

    GglError ret = add_epoll_watch(epoll_fd, client_fd, handle);
    if (ret != GGL_ERR_OK) {
        bool released = release_client_fd(client_pool, handle, &client_fd);
        if (released) {
            close(client_fd);
        }
        GGL_LOGE("socket-server", "Failed to register client fd with epoll.");
        return;
    }

    GGL_LOGD("socket-server", "Accepted client connection.");
}

static void client_data_ready(
    SocketServerClientPool *client_pool,
    uint32_t handle,
    GglError (*client_ready)(void *ctx, uint32_t handle),
    void *ctx
) {
    assert(client_ready != NULL);

    GglError ret = client_ready(ctx, handle);
    if (ret != GGL_ERR_OK) {
        ggl_socket_close(client_pool, handle);
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
    SocketServerClientPool *client_pool,
    GglError (*client_ready)(void *ctx, ClientHandle handle),
    void *ctx
) {
    assert(socket_path != NULL);
    assert(client_pool != NULL);
    assert(client_pool->fds != NULL);
    assert(client_pool->generations != NULL);
    assert(client_ready != NULL);

    signal(SIGPIPE, SIG_IGN);

    if (client_pool->max_clients >= UINT16_MAX) {
        GGL_LOGE("socket-server", "Max clients larger than supported.");
        return GGL_ERR_FAILURE;
    }

    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd == -1) {
        int err = errno;
        GGL_LOGE("socket-server", "Failed to create socket: %d.", err);
        return GGL_ERR_FAILURE;
    }
    GGL_DEFER(close, server_fd);

    fcntl(server_fd, F_SETFD, FD_CLOEXEC);

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

    // server_fd data larger than uint32_t to not conflict with handles
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
            uint64_t event_data = events[i].data.u64;
            if (event_data == UINT64_MAX) {
                new_client_ready(client_pool, epoll_fd, server_fd);
            } else if (event_data <= UINT32_MAX) {
                client_data_ready(
                    client_pool, (uint32_t) event_data, client_ready, ctx
                );
            } else {
                GGL_LOGE("socket-server", "Invalid data returned from epoll");
                return GGL_ERR_FAILURE;
            }
        }
    }

    return GGL_ERR_FAILURE;
}

static GglError recv_wrapper(
    SocketServerClientPool *client_pool, ClientHandle handle, GglBuffer *rest
) {
    uint16_t index = (uint16_t) (handle & UINT16_MAX);
    uint16_t generation = (uint16_t) (handle >> 16);

    pthread_mutex_lock(&client_fd_mtx);
    GGL_DEFER(pthread_mutex_unlock, client_fd_mtx);

    if (generation != client_pool->generations[index]) {
        GGL_LOGD("socket-server", "Generation mismatch in %s.", __func__);
        return GGL_ERR_NOCONN;
    }

    int fd = client_pool->fds[index];

    ssize_t ret = recv(fd, rest->data, rest->len, MSG_WAITALL);
    if (ret < 0) {
        if (errno == EINTR) {
            return GGL_ERR_OK;
        }
        int err = errno;
        GGL_LOGE("socket-server", "Failed to recv from client: %d.", err);
        return GGL_ERR_FAILURE;
    }
    if (ret == 0) {
        GGL_LOGD("socket-server", "Client socket closed");
        return GGL_ERR_NOCONN;
    }

    *rest = ggl_buffer_substr(*rest, (size_t) ret, SIZE_MAX);
    return GGL_ERR_OK;
}

GglError ggl_socket_read(
    SocketServerClientPool *client_pool, ClientHandle handle, GglBuffer buf
) {
    GglBuffer rest = buf;

    while (rest.len > 0) {
        GglError ret = recv_wrapper(client_pool, handle, &rest);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    return GGL_ERR_OK;
}

static GglError write_wrapper(
    SocketServerClientPool *client_pool, ClientHandle handle, GglBuffer *rest
) {
    uint16_t index = (uint16_t) (handle & UINT16_MAX);
    uint16_t generation = (uint16_t) (handle >> 16);

    pthread_mutex_lock(&client_fd_mtx);
    GGL_DEFER(pthread_mutex_unlock, client_fd_mtx);

    if (generation != client_pool->generations[index]) {
        GGL_LOGD("socket-server", "Generation mismatch in %s.", __func__);
        return GGL_ERR_NOCONN;
    }

    int fd = client_pool->fds[index];

    ssize_t ret = write(fd, rest->data, rest->len);
    if (ret < 0) {
        if (errno == EINTR) {
            return GGL_ERR_OK;
        }
        int err = errno;
        if (errno != EPIPE) {
            GGL_LOGE("socket-server", "Failed to write to client: %d.", err);
        }
        return GGL_ERR_FAILURE;
    }

    *rest = ggl_buffer_substr(*rest, (size_t) ret, SIZE_MAX);
    return GGL_ERR_OK;
}

GglError ggl_socket_write(
    SocketServerClientPool *client_pool, ClientHandle handle, GglBuffer buf
) {
    GglBuffer rest = buf;

    while (rest.len > 0) {
        GglError ret = write_wrapper(client_pool, handle, &rest);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    return GGL_ERR_OK;
}

GglError ggl_socket_close(
    SocketServerClientPool *client_pool, ClientHandle handle
) {
    int client_fd = -1;
    bool released = release_client_fd(client_pool, handle, &client_fd);
    if (released) {
        close(client_fd);
    }
    return GGL_ERR_OK;
}

GglError ggl_socket_with_index(
    void (*action)(void *ctx, size_t index),
    void *ctx,
    SocketServerClientPool *client_pool,
    ClientHandle handle
) {
    uint16_t index = (uint16_t) (handle & UINT16_MAX);
    uint16_t generation = (uint16_t) (handle >> 16);

    pthread_mutex_lock(&client_fd_mtx);
    GGL_DEFER(pthread_mutex_unlock, client_fd_mtx);

    if (generation != client_pool->generations[index]) {
        GGL_LOGD("socket-server", "Generation mismatch in %s.", __func__);
        return GGL_ERR_NOCONN;
    }

    action(ctx, index);

    return GGL_ERR_OK;
}
