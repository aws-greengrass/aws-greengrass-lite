// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/socket_handle.h"
#include "ggl/socket.h"
#include <assert.h>
#include <ggl/alloc.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>
#include <stdint.h>

static const int32_t FD_FREE = -0x55555556; // Alternating bits for debugging

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
            uint32_t new_handle = (uint32_t) pool->generations[i] << 16 | i;

            if (pool->on_register != NULL) {
                GglError ret = pool->on_register(new_handle, i);
                if (ret != GGL_ERR_OK) {
                    pool->fds[i] = FD_FREE;
                    return ret;
                }
            }

            *handle = new_handle;

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

    if (pool->on_release != NULL) {
        GglError ret = pool->on_release(handle, index);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    if (fd != NULL) {
        *fd = pool->fds[index];
    }

    pool->generations[index] += 1;
    pool->fds[index] = FD_FREE;

    GGL_LOGD(
        "socket",
        "Releasing fd %d at index %u, generation %u.",
        *fd,
        index,
        generation
    );

    return GGL_ERR_OK;
}

GglError ggl_socket_handle_read(
    GglSocketPool *pool, uint32_t handle, GglBuffer buf
) {
    GglBuffer rest = buf;

    uint16_t index = (uint16_t) (handle & UINT16_MAX);
    uint16_t generation = (uint16_t) (handle >> 16);

    while (rest.len > 0) {
        pthread_mutex_lock(&pool->mtx);
        GGL_DEFER(unlock_pool_mtx, pool);

        if (generation != pool->generations[index]) {
            GGL_LOGD("socket", "Generation mismatch in %s.", __func__);
            return GGL_ERR_NOENTRY;
        }

        GglError ret = ggl_read(pool->fds[index], &rest);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    return GGL_ERR_OK;
}

GglError ggl_socket_handle_write(
    GglSocketPool *pool, uint32_t handle, GglBuffer buf
) {
    GglBuffer rest = buf;

    uint16_t index = (uint16_t) (handle & UINT16_MAX);
    uint16_t generation = (uint16_t) (handle >> 16);

    while (rest.len > 0) {
        pthread_mutex_lock(&pool->mtx);
        GGL_DEFER(unlock_pool_mtx, pool);

        if (generation != pool->generations[index]) {
            GGL_LOGD("socket", "Generation mismatch in %s.", __func__);
            return GGL_ERR_NOENTRY;
        }

        GglError ret = ggl_write(pool->fds[index], &rest);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    return GGL_ERR_OK;
}

GglError ggl_socket_handle_close(GglSocketPool *pool, uint32_t handle) {
    int fd = -1;

    GglError ret = ggl_socket_pool_release(pool, handle, &fd);
    if (ret == GGL_ERR_OK) {
        close(fd);
    }

    return ret;
}

GglError ggl_with_socket_handle_index(
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
