/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ggl/socket.h"
#include <assert.h>
#include <errno.h>
#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stddef.h>
#include <stdint.h>

static const int32_t FD_FREE = -0x55555556; /* Alternating bits for debugging */

GGL_DEFINE_DEFER(
    unlock_pool_mtx, GglSocketPool *, pool, pthread_mutex_unlock(&(*pool)->mtx)
)

void ggl_socket_pool_init(GglSocketPool *pool) {
    assert(pool != NULL);
    assert(pool->fds != NULL);
    assert(pool->generations != NULL);

    for (size_t i = 0; i < pool->max_fds; i++) {
        pool->fds[i] = FD_FREE;
    }
}

GglError ggl_socket_pool_register(
    GglSocketPool *pool, int fd, uint32_t *handle
) {
    if (fd < 0) {
        return GGL_ERR_INVALID;
    }

    pthread_mutex_lock(&pool->mtx);
    GGL_DEFER(unlock_pool_mtx, pool);

    for (uint16_t i = 0; i < pool->max_fds; i++) {
        if (pool->fds[i] == FD_FREE) {
            pool->fds[i] = fd;
            *handle = (uint32_t) pool->generations[i] << 16 | i;

            if (pool->on_register != NULL) {
                pool->on_register(*handle, i);
            }

            GGL_LOGD(
                "socket",
                "Registered fd %d at index %u, generation %u.",
                fd,
                i,
                pool->generations[i]
            );

            return GGL_ERR_OK;
        }
    }

    return GGL_ERR_NOMEM;
}

GglError ggl_socket_pool_release(
    GglSocketPool *pool, uint32_t handle, int *fd
) {
    uint16_t index = (uint16_t) (handle & UINT16_MAX);
    uint16_t generation = (uint16_t) (handle >> 16);

    pthread_mutex_lock(&pool->mtx);
    GGL_DEFER(unlock_pool_mtx, pool);

    if (generation != pool->generations[index]) {
        GGL_LOGD("socket", "Generation mismatch in %s.", __func__);
        return GGL_ERR_NOENTRY;
    }

    *fd = pool->fds[index];

    pool->generations[index] += 1;
    pool->fds[index] = FD_FREE;

    if (pool->on_release != NULL) {
        pool->on_release(handle, index);
    }

    GGL_LOGD(
        "socket",
        "Releasing fd %d at index %u, generation %u.",
        *fd,
        index,
        generation
    );

    return GGL_ERR_OK;
}

static GglError recv_wrapper(
    GglSocketPool *pool, uint32_t handle, GglBuffer *rest
) {
    uint16_t index = (uint16_t) (handle & UINT16_MAX);
    uint16_t generation = (uint16_t) (handle >> 16);

    pthread_mutex_lock(&pool->mtx);
    GGL_DEFER(unlock_pool_mtx, pool);

    if (generation != pool->generations[index]) {
        GGL_LOGD("socket", "Generation mismatch in %s.", __func__);
        return GGL_ERR_NOENTRY;
    }

    int fd = pool->fds[index];

    ssize_t ret = recv(fd, rest->data, rest->len, MSG_WAITALL);
    if (ret < 0) {
        if (errno == EINTR) {
            return GGL_ERR_OK;
        }
        if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
            GGL_LOGE("socket", "recv timed out on socket %d.", fd);
            return GGL_ERR_FAILURE;
        }
        int err = errno;
        GGL_LOGE("socket", "Failed to recv from client: %d.", err);
        return GGL_ERR_FAILURE;
    }
    if (ret == 0) {
        GGL_LOGD("socket", "Client socket closed.");
        return GGL_ERR_NOCONN;
    }

    *rest = ggl_buffer_substr(*rest, (size_t) ret, SIZE_MAX);
    return GGL_ERR_OK;
}

GglError ggl_socket_read(GglSocketPool *pool, uint32_t handle, GglBuffer buf) {
    GglBuffer rest = buf;

    while (rest.len > 0) {
        GglError ret = recv_wrapper(pool, handle, &rest);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    return GGL_ERR_OK;
}

static GglError write_wrapper(
    GglSocketPool *pool, uint32_t handle, GglBuffer *rest
) {
    uint16_t index = (uint16_t) (handle & UINT16_MAX);
    uint16_t generation = (uint16_t) (handle >> 16);

    pthread_mutex_lock(&pool->mtx);
    GGL_DEFER(unlock_pool_mtx, pool);

    if (generation != pool->generations[index]) {
        GGL_LOGD("socket", "Generation mismatch in %s.", __func__);
        return GGL_ERR_NOENTRY;
    }

    int fd = pool->fds[index];

    ssize_t ret = write(fd, rest->data, rest->len);
    if (ret < 0) {
        if (errno == EINTR) {
            return GGL_ERR_OK;
        }
        if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
            GGL_LOGE("socket", "write timed out on socket %d.", fd);
            return GGL_ERR_FAILURE;
        }
        int err = errno;
        if (errno != EPIPE) {
            GGL_LOGE("socket", "Failed to write to client: %d.", err);
        }
        return GGL_ERR_FAILURE;
    }

    *rest = ggl_buffer_substr(*rest, (size_t) ret, SIZE_MAX);
    return GGL_ERR_OK;
}

GglError ggl_socket_write(GglSocketPool *pool, uint32_t handle, GglBuffer buf) {
    GglBuffer rest = buf;

    while (rest.len > 0) {
        GglError ret = write_wrapper(pool, handle, &rest);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    return GGL_ERR_OK;
}

GglError ggl_socket_close(GglSocketPool *pool, uint32_t handle) {
    int fd = -1;

    GglError ret = ggl_socket_pool_release(pool, handle, &fd);
    if (ret == GGL_ERR_OK) {
        close(fd);
    }

    return ret;
}

GglError ggl_socket_with_index(
    void (*action)(void *ctx, size_t index),
    void *ctx,
    GglSocketPool *pool,
    uint32_t handle
) {
    uint16_t index = (uint16_t) (handle & UINT16_MAX);
    uint16_t generation = (uint16_t) (handle >> 16);

    pthread_mutex_lock(&pool->mtx);
    GGL_DEFER(unlock_pool_mtx, pool);

    if (generation != pool->generations[index]) {
        GGL_LOGD("socket", "Generation mismatch in %s.", __func__);
        return GGL_ERR_NOENTRY;
    }

    action(ctx, index);

    return GGL_ERR_OK;
}
