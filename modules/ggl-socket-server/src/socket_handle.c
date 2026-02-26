// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/file.h>
#include <gg/io.h>
#include <gg/log.h>
#include <gg/types.h>
#include <ggl/socket_handle.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <stddef.h>
#include <stdint.h>

// Handles are 32 bits, with the high 16 bits being a generation counter, and
// the low 16 bits being an offset index. The generation counter is incremented
// on close, to prevent reuse.
//
// Use of the index and generation count must be done with a mutex held to
// prevent concurrent incrementing of the generation counter.
//
// The index is offset by 1 in order to ensure 0 is not a valid handle,
// preventing a zero-initialized handle from accidentally working. Since the
// array length (pool->max_fds) is in the range [0, UINT16_MAX], valid indices
// are in the range [0, UINT16_MAX - 1]. Thus incrementing the index will not
// overflow a uint16_t.

static const int32_t FD_FREE = -0x55555556; // Alternating bits for debugging

static GgError validate_handle(
    GglSocketPool *pool, uint32_t handle, uint16_t *index, const char *location
) {
    // Underflow ok; UINT16_MAX will fail bounds check
    uint16_t handle_index = (uint16_t) ((handle & UINT16_MAX) - 1U);
    uint16_t handle_generation = (uint16_t) (handle >> 16);

    if (handle_index >= pool->max_fds) {
        GG_LOGE("Invalid handle %u in %s.", handle, location);
        return GG_ERR_INVALID;
    }

    if (handle_generation != pool->generations[handle_index]) {
        GG_LOGD("Generation mismatch for handle %d in %s.", handle, location);
        return GG_ERR_NOENTRY;
    }

    *index = handle_index;
    return GG_ERR_OK;
}

void ggl_socket_pool_init(GglSocketPool *pool) {
    assert(pool != NULL);
    assert(pool->fds != NULL);
    assert(pool->generations != NULL);

    GG_LOGT("Initializing socket pool %p.", pool);

    for (size_t i = 0; i < pool->max_fds; i++) {
        pool->fds[i] = FD_FREE;
    }

    // TODO: handle mutex init failure?
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&pool->mtx, &attr);
}

GgError ggl_socket_pool_register(
    GglSocketPool *pool, int fd, uint32_t *handle
) {
    assert(handle != NULL);

    GG_LOGT("Registering fd %d in pool %p.", fd, pool);

    if (fd < 0) {
        GG_LOGE("%s received invalid fd: %d.", __func__, fd);
        return GG_ERR_INVALID;
    }

    GG_MTX_SCOPE_GUARD(&pool->mtx);

    for (uint16_t i = 0; i < pool->max_fds; i++) {
        if (pool->fds[i] == FD_FREE) {
            pool->fds[i] = fd;
            uint32_t new_handle
                = (uint32_t) pool->generations[i] << 16 | (i + 1U);

            if (pool->on_register != NULL) {
                GgError ret = pool->on_register(new_handle, i);
                if (ret != GG_ERR_OK) {
                    pool->fds[i] = FD_FREE;
                    GG_LOGE("Pool on_register callback failed.");
                    return ret;
                }
            }

            *handle = new_handle;

            GG_LOGD(
                "Registered fd %d at index %u, generation %u with handle %u.",
                fd,
                i,
                pool->generations[i],
                new_handle
            );

            // coverity[missing_restore]
            return GG_ERR_OK;
        }
    }

    GG_LOGE("Pool maximum fds exceeded.");
    return GG_ERR_NOMEM;
}

GgError ggl_socket_pool_release(GglSocketPool *pool, uint32_t handle, int *fd) {
    GG_LOGT("Releasing handle %u in pool %p.", handle, pool);

    GG_MTX_SCOPE_GUARD(&pool->mtx);

    uint16_t index = 0;
    GgError ret = validate_handle(pool, handle, &index, __func__);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    if (pool->on_release != NULL) {
        ret = pool->on_release(handle, index);
        if (ret != GG_ERR_OK) {
            GG_LOGE(
                "Pool on_release callback failed for fd %d, index %u, generation %u.",
                pool->fds[index],
                index,
                pool->generations[index]
            );
            return ret;
        }
    }

    if (fd != NULL) {
        *fd = pool->fds[index];
    }

    GG_LOGD(
        "Releasing fd %d at index %u, generation %u.",
        pool->fds[index],
        index,
        pool->generations[index]
    );

    pool->generations[index] += 1;
    pool->fds[index] = FD_FREE;

    return GG_ERR_OK;
}

GgError ggl_socket_handle_read(
    GglSocketPool *pool, uint32_t handle, GgBuffer buf
) {
    GG_LOGT(
        "Reading %zu bytes from handle %u in pool %p.", buf.len, handle, pool
    );

    GgBuffer rest = buf;

    while (rest.len > 0) {
        GG_MTX_SCOPE_GUARD(&pool->mtx);

        uint16_t index = 0;
        GgError ret = validate_handle(pool, handle, &index, __func__);
        if (ret != GG_ERR_OK) {
            return ret;
        }

        ret = gg_file_read_partial(pool->fds[index], &rest);
        if (ret == GG_ERR_RETRY) {
            continue;
        }
        if (ret != GG_ERR_OK) {
            return ret;
        }
    }

    GG_LOGT("Read from %u successful.", handle);
    return GG_ERR_OK;
}

GgError ggl_socket_handle_write(
    GglSocketPool *pool, uint32_t handle, GgBuffer buf
) {
    GG_LOGT(
        "Writing %zu bytes to handle %u in pool %p.", buf.len, handle, pool
    );

    GgBuffer rest = buf;

    while (rest.len > 0) {
        GG_MTX_SCOPE_GUARD(&pool->mtx);

        uint16_t index = 0;
        GgError ret = validate_handle(pool, handle, &index, __func__);
        if (ret != GG_ERR_OK) {
            return ret;
        }

        ret = gg_file_write_partial(pool->fds[index], &rest);
        if (ret == GG_ERR_RETRY) {
            continue;
        }
        if (ret != GG_ERR_OK) {
            return ret;
        }
    }

    GG_LOGT("Write to %u successful.", handle);
    return GG_ERR_OK;
}

GgError ggl_socket_handle_close(GglSocketPool *pool, uint32_t handle) {
    GG_LOGT("Closing handle %u in pool %p.", handle, pool);

    int fd = -1;

    GgError ret = ggl_socket_pool_release(pool, handle, &fd);
    if (ret == GG_ERR_OK) {
        (void) gg_close(fd);
    }

    GG_LOGT("Close of %u successful.", handle);
    return ret;
}

GgError ggl_socket_handle_get_peer_pid(
    GglSocketPool *pool, uint32_t handle, pid_t *pid
) {
    GG_LOGT("Getting peer pid for handle %u in pool %p.", handle, pool);

    GG_MTX_SCOPE_GUARD(&pool->mtx);

    uint16_t index = 0;
    GgError ret = validate_handle(pool, handle, &index, __func__);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    struct ucred ucred;
    socklen_t ucred_len = sizeof(ucred);
    if ((getsockopt(
             pool->fds[index], SOL_SOCKET, SO_PEERCRED, &ucred, &ucred_len
         )
         != 0)
        || (ucred_len != sizeof(ucred))) {
        GG_LOGE("Failed to get peer cred for fd %d.", pool->fds[index]);
        return GG_ERR_FAILURE;
    }

    *pid = ucred.pid;
    GG_LOGT("Get pid for %u successful (%d).", handle, ucred.pid);
    return GG_ERR_OK;
}

GgError ggl_socket_handle_protected(
    void (*action)(void *ctx, size_t index),
    void *ctx,
    GglSocketPool *pool,
    uint32_t handle
) {
    GG_LOGT("In %s with handle %u in pool %p.", __func__, handle, pool);

    GG_MTX_SCOPE_GUARD(&pool->mtx);

    uint16_t index = 0;
    GgError ret = validate_handle(pool, handle, &index, __func__);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    action(ctx, index);

    GG_LOGT(
        "Successfully completed %s with handle %u in pool %p.",
        __func__,
        handle,
        pool
    );
    return GG_ERR_OK;
}

static GgError socket_handle_reader_fn(void *ctx, GgBuffer *buf) {
    GglSocketHandleReaderCtx *args = ctx;
    return ggl_socket_handle_read(args->pool, args->handle, *buf);
}

GgReader ggl_socket_handle_reader(
    GglSocketHandleReaderCtx *ctx, GglSocketPool *pool, uint32_t handle
) {
    ctx->pool = pool;
    ctx->handle = handle;
    return (GgReader) { .read = socket_handle_reader_fn, .ctx = ctx };
}
