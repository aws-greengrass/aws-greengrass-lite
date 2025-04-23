// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <sys/types.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <ggl/buffer.h>
#include <ggl/cleanup.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/log.h>
#include <ggl/socket_epoll.h>
#include <ggl/socket_handle.h>
#include <ggl/socket_server.h>
#include <limits.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

void (*ggl_socket_server_ext_handler)(void) = NULL;
int ggl_socket_server_ext_fd;

static void new_client_available(
    GglSocketPool *pool, int epoll_fd, int socket_fd
) {
    assert(epoll_fd >= 0);
    assert(socket_fd >= 0);

    int client_fd = accept4(socket_fd, NULL, NULL, SOCK_CLOEXEC);
    if (client_fd == -1) {
        int err = errno;
        GGL_LOGE("Failed to accept on socket %d: %d.", socket_fd, err);
        return;
    }
    GGL_CLEANUP_ID(client_fd_cleanup, cleanup_close, client_fd);

    GGL_LOGD("Accepted new client %d.", client_fd);

    // To prevent deadlocking on hanged client, add a timeout
    struct timeval timeout = { .tv_sec = 5 };
    int sys_ret = setsockopt(
        client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)
    );
    if (sys_ret == -1) {
        GGL_LOGE("Failed to set send timeout on %d: %d.", client_fd, errno);
        return;
    }
    sys_ret = setsockopt(
        client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)
    );
    if (sys_ret == -1) {
        GGL_LOGE("Failed to set receive timeout on %d: %d.", client_fd, errno);
        return;
    }

    uint32_t handle = 0;
    GglError ret = ggl_socket_pool_register(pool, client_fd, &handle);
    if (ret != GGL_ERR_OK) {
        GGL_LOGW("Closed new client %d due to max clients reached.", client_fd);
        return;
    }

    // Socket is now owned by the pool
    // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores) false positive
    client_fd_cleanup = -1;

    ret = ggl_socket_epoll_add(epoll_fd, client_fd, handle);
    if (ret != GGL_ERR_OK) {
        ggl_socket_handle_close(pool, handle);
        GGL_LOGE("Failed to register client %d with epoll.", client_fd);
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
        ggl_socket_handle_close(pool, handle);
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

static GglError configure_server_socket(
    int socket_fd, GglBuffer path, mode_t mode
) {
    assert(socket_fd >= 0);

    struct sockaddr_un addr = { .sun_family = AF_UNIX, .sun_path = { 0 } };

    // TODO: Handle long paths by creating socket in temp dir and moving
    if (path.len >= sizeof(addr.sun_path)) {
        GGL_LOGE(
            "Socket path too long (len %zu, max %zu).",
            path.len,
            sizeof(addr.sun_path) - 1
        );
        return GGL_ERR_FAILURE;
    }

    memcpy(addr.sun_path, path.data, path.len);

    GglError ret = create_parent_dirs(addr.sun_path);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if ((unlink(addr.sun_path) == -1) && (errno != ENOENT)) {
        int err = errno;
        GGL_LOGE("Failed to unlink server socket: %d.", err);
        return GGL_ERR_FAILURE;
    }

    if (bind(socket_fd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
        int err = errno;
        GGL_LOGE("Failed to bind server socket: %d.", err);
        return GGL_ERR_FAILURE;
    }

    if (chmod(addr.sun_path, mode) == -1) {
        GGL_LOGE("Failed to chmod server socket: %d.", errno);
        return GGL_ERR_FAILURE;
    }

    static const int MAX_SOCKET_BACKLOG = 10;
    if (listen(socket_fd, MAX_SOCKET_BACKLOG) == -1) {
        int err = errno;
        GGL_LOGE("Failed to listen on server socket: %d.", err);
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

static const uint64_t EXT_FD_DATA = UINT64_MAX - 1;

static GglError epoll_fd_ready(void *epoll_ctx, uint64_t data) {
    SocketServerCtx *server_ctx = epoll_ctx;

    if (data == SERVER_FD_DATA) {
        new_client_available(
            server_ctx->pool, server_ctx->epoll_fd, server_ctx->server_fd
        );
    } else if ((ggl_socket_server_ext_handler != NULL)
               && (data == EXT_FD_DATA)) {
        ggl_socket_server_ext_handler();
    } else if (data <= UINT32_MAX) {
        client_data_ready(
            server_ctx->pool,
            (uint32_t) data,
            server_ctx->client_ready,
            server_ctx->ctx
        );
    } else {
        GGL_LOGE("Invalid data returned from epoll.");
        return GGL_ERR_FAILURE;
    }

    return GGL_ERR_OK;
}

static GglBuffer split_fd_name_buffer(GglBuffer names, GglBuffer *name) {
    for (size_t i = 0; i < names.len; ++i) {
        if (names.data[i] == ':') {
            *name = ggl_buffer_substr(names, 0, i);
            return ggl_buffer_substr(names, i + 1, SIZE_MAX);
        }
    }
    *name = names;
    return ggl_buffer_substr(names, names.len, SIZE_MAX);
}

static const int FD_SOCKET_ACTIVATION_START = 3;

static bool validate_server_socket(int server_fd) {
    struct stat statbuf;
    int ret = fstat(server_fd, &statbuf);
    if (ret == -1) {
        return false;
    }
    return S_ISSOCK(statbuf.st_mode);
}

static int inherit_socket_from_env(GglBuffer socket_name) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe) safe on glibc; not calling setenv
    char *pid_env = getenv("LISTEN_PID");
    // validate the sockets are for this PID
    if ((pid_env != NULL) && (pid_env[0] != '\0')) {
        GGL_LOGT("LISTEN_PID: %s", pid_env);
        int64_t pid;
        GglError err
            = ggl_str_to_int64(ggl_buffer_from_null_term(pid_env), &pid);
        if (err != GGL_ERR_OK) {
            return -1;
        }
        if (pid != getpid()) {
            GGL_LOGD("Socket was not intended for this PID.");
            return -1;
        }
    }

    // NOLINTNEXTLINE(concurrency-mt-unsafe) safe on glibc; not calling setenv
    char *fds_env = getenv("LISTEN_FDS");
    if ((fds_env == NULL) || (fds_env[0] == '\0')) {
        return -1;
    }
    GGL_LOGT("LISTEN_FDS: %s", fds_env);
    int64_t fd_count;
    GglError err
        = ggl_str_to_int64(ggl_buffer_from_null_term(fds_env), &fd_count);
    if (err != GGL_ERR_OK) {
        return -1;
    }
    // validate fd_count
    if ((fd_count < 0) || (fd_count > (INT_MAX - FD_SOCKET_ACTIVATION_START))) {
        GGL_LOGD("Socket activation fd count not valid.");
        return -1;
    }
    int max_fd = (int) fd_count + FD_SOCKET_ACTIVATION_START;
    struct rlimit rlim;
    if (getrlimit(RLIMIT_NOFILE, &rlim) != -1) {
        if ((uintmax_t) max_fd > rlim.rlim_cur) {
            GGL_LOGD("Socket activation fd count too big.");
            return -1;
        }
    }

    // NOLINTNEXTLINE(concurrency-mt-unsafe) safe on glibc; not calling setenv
    char *names_env = getenv("LISTEN_FDNAMES");
    if ((names_env == NULL) || (names_env[0] == '\0')) {
        return -1;
    }
    GGL_LOGT("LISTEN_FDNAMES: \"%s\"", names_env);

    // LISTEN_FDNAMES is a colon-delimited list of ASCII strings
    GglBuffer names = ggl_buffer_from_null_term(names_env);
    for (int server_fd = FD_SOCKET_ACTIVATION_START; server_fd < max_fd;
         ++server_fd) {
        GglBuffer name;
        names = split_fd_name_buffer(names, &name);
        if (name.len == 0) {
            break;
        }
        if (ggl_buffer_eq(name, socket_name)) {
            GGL_LOGT("Found socket.");
            if (!validate_server_socket(server_fd)) {
                GGL_LOGD("Socket fd not open or not a socket.");
                return -1;
            }
            GGL_LOGT("Configuring socket.");
            int flags = fcntl(server_fd, F_GETFD, 0);
            flags |= O_CLOEXEC;
            flags &= ~O_NONBLOCK;
            fcntl(server_fd, F_SETFD, flags);
            return server_fd;
        }
    }
    return -1;
}

GglError ggl_socket_server_listen(
    const GglBuffer *socket_name,
    GglBuffer path,
    mode_t mode,
    GglSocketPool *pool,
    GglError (*client_ready)(void *ctx, uint32_t handle),
    void *ctx
) {
    assert(pool != NULL);
    assert(pool->fds != NULL);
    assert(pool->generations != NULL);
    assert(client_ready != NULL);

    int epoll_fd;
    GglError ret = ggl_socket_epoll_create(&epoll_fd);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    GGL_CLEANUP(cleanup_close, epoll_fd);

    int server_fd = -1;
    if (socket_name != NULL) {
        GGL_LOGD(
            "Attempting socket activation on %.*s",
            (int) socket_name->len,
            socket_name->data
        );
        server_fd = inherit_socket_from_env(*socket_name);
    }
    // If socket activation is not attempted or fails, create one
    if (server_fd == -1) {
        GGL_LOGD("Falling back to creating socket.");
        server_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (server_fd == -1) {
            GGL_LOGE("Failed to create socket: %d.", errno);
            return GGL_ERR_FAILURE;
        }

        ret = configure_server_socket(server_fd, path, mode);
        if (ret != GGL_ERR_OK) {
            cleanup_close(&server_fd);
            return ret;
        }
        GGL_LOGT("Listening on %.*s", (int) path.len, path.data);
    } else {
        GGL_LOGT(
            "Received listen socket %.*s",
            (int) socket_name->len,
            socket_name->data
        );
    }
    GGL_CLEANUP(cleanup_close, server_fd);

    ret = ggl_socket_epoll_add(epoll_fd, server_fd, SERVER_FD_DATA);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (ggl_socket_server_ext_handler != NULL) {
        ret = ggl_socket_epoll_add(
            epoll_fd, ggl_socket_server_ext_fd, EXT_FD_DATA
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    SocketServerCtx server_ctx = {
        .pool = pool,
        .epoll_fd = epoll_fd,
        .server_fd = server_fd,
        .client_ready = client_ready,
        .ctx = ctx,
    };

    return ggl_socket_epoll_run(epoll_fd, epoll_fd_ready, &server_ctx);
}
