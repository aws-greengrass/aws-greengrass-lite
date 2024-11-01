// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/core_bus/sub_response.h"
#include <sys/types.h>
#include <assert.h>
#include <errno.h>
#include <ggl/buffer.h>
#include <ggl/cleanup.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <inttypes.h>
#include <pthread.h>
#include <time.h>
#include <stdatomic.h>
#include <stdbool.h>

typedef struct GglSubResponseCallbackCtx {
    pthread_mutex_t *mtx;
    pthread_cond_t *cond;
    GglSubResponseCallback callback;
    void *callback_ctx;
    GglError response_error;
    atomic_bool ready;
} GglSubResponseCallbackCtx;

static GglError sub_response_on_response(
    void *ctx, uint32_t handle, GglObject data
) {
    GGL_LOGD("Receiving response for %" PRIu32, handle);
    GglSubResponseCallbackCtx *context = (GglSubResponseCallbackCtx *) ctx;
    GglError err = context->callback(ctx, data);
    context->response_error = err;
    return (err == GGL_ERR_OK) ? GGL_ERR_EXPECTED : err;
}

static void sub_response_on_close(void *ctx, uint32_t handle) {
    GglSubResponseCallbackCtx *context = (GglSubResponseCallbackCtx *) ctx;
    GGL_LOGD("Notifying response for %" PRIu32, handle);
    GGL_MTX_SCOPE_GUARD(context->mtx);
    atomic_store_explicit(&context->ready, true, memory_order_release);
    pthread_cond_signal(context->cond);
}

GglError ggl_sub_response(
    GglBuffer interface,
    GglBuffer method,
    GglMap params,
    GglSubResponseCallback callback,
    void *ctx,
    GglError *method_error,
    int64_t timeout_seconds
) {
    assert(callback != NULL);
    pthread_cond_t cond;
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&cond, &attr);
    GGL_CLEANUP(pthread_condattr_destroy, attr);

    pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    GglSubResponseCallbackCtx resp_ctx = { .response_error = GGL_ERR_FAILURE,
                                           .ready = false,
                                           .callback = callback,
                                           .callback_ctx = ctx,
                                           .mtx = &mtx,
                                           .cond = &cond };
    uint32_t handle = 0;
    GglError subscribe_error = GGL_ERR_OK;

    struct timespec timeout_abs;
    clock_gettime(CLOCK_MONOTONIC, &timeout_abs);
    timeout_abs.tv_sec += timeout_seconds;

    bool ready = false;
    {
        GGL_MTX_SCOPE_GUARD_ID(lock, &mtx);
        subscribe_error = ggl_subscribe(
            interface,
            method,
            params,
            sub_response_on_response,
            sub_response_on_close,
            &resp_ctx,
            method_error,
            &handle
        );
        if (subscribe_error != GGL_ERR_OK) {
            return subscribe_error;
        }
        while (!(
            ready = atomic_load_explicit(&resp_ctx.ready, memory_order_acquire)
        )) {
            int cond_ret = pthread_cond_timedwait(&cond, lock, &timeout_abs);
            if ((cond_ret < 0) && (cond_ret != EINTR)) {
                assert(cond_ret == ETIMEDOUT);
                GGL_LOGW("Timed out waiting for a response.");
                break;
            }
        }
    }

    // timeout handling
    if (!ready) {
        ggl_client_sub_close(handle);
    }

    GglError err = resp_ctx.response_error;
    if (err != GGL_ERR_OK) {
        return err;
    }

    GGL_LOGD("Finished waiting for a response.");
    return GGL_ERR_OK;
}
