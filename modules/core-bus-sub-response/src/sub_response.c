// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <errno.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/log.h>
#include <gg/types.h>
#include <ggl/core_bus/client.h>
#include <ggl/core_bus/sub_response.h>
#include <inttypes.h>
#include <pthread.h>
#include <sys/types.h>
#include <time.h>
#include <stdbool.h>

typedef struct GglSubResponseCallbackCtx {
    pthread_mutex_t *mtx;
    pthread_cond_t *cond;
    bool ready;
    GglSubResponseCallback callback;
    void *callback_ctx;
    GgError response_error;
} GglSubResponseCallbackCtx;

static void cleanup_pthread_cond(pthread_cond_t **cond) {
    pthread_cond_destroy(*cond);
}

static GgError sub_response_on_response(
    void *ctx, uint32_t handle, GgObject data
) {
    GG_LOGD("Receiving response for %" PRIu32, handle);
    GglSubResponseCallbackCtx *context = ctx;

    GgError err = context->callback(ctx, data);

    if (err == GG_ERR_RETRY) {
        // Skip this response
        return GG_ERR_OK;
    }

    context->response_error = err;
    // Err to close subscription
    return GG_ERR_EXPECTED;
}

static void sub_response_on_close(void *ctx, uint32_t handle) {
    GglSubResponseCallbackCtx *context = ctx;
    GG_LOGD("Notifying response for %" PRIu32, handle);
    GG_MTX_SCOPE_GUARD(context->mtx);
    context->ready = true;
    pthread_cond_signal(context->cond);
}

GgError ggl_sub_response(
    GgBuffer interface,
    GgBuffer method,
    GgMap params,
    GglSubResponseCallback callback,
    void *ctx,
    GgError *remote_error,
    int64_t timeout_seconds
) {
    assert(callback != NULL);

    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_t cond;
    pthread_cond_init(&cond, &attr);
    pthread_condattr_destroy(&attr);
    GG_CLEANUP(cleanup_pthread_cond, &cond);
    pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

    GglSubResponseCallbackCtx resp_ctx = { .response_error = GG_ERR_FAILURE,
                                           .ready = false,
                                           .callback = callback,
                                           .callback_ctx = ctx,
                                           .mtx = &mtx,
                                           .cond = &cond };
    uint32_t handle = 0;

    struct timespec timeout_abs;
    clock_gettime(CLOCK_MONOTONIC, &timeout_abs);
    timeout_abs.tv_sec += timeout_seconds;

    GgError subscribe_error = ggl_subscribe(
        interface,
        method,
        params,
        sub_response_on_response,
        sub_response_on_close,
        &resp_ctx,
        remote_error,
        &handle
    );
    if (subscribe_error != GG_ERR_OK) {
        return subscribe_error;
    }

    bool timed_out = false;

    {
        GG_MTX_SCOPE_GUARD(&mtx);

        while (!resp_ctx.ready) {
            int cond_ret = pthread_cond_timedwait(&cond, &mtx, &timeout_abs);
            if ((cond_ret != 0) && (cond_ret != EINTR)) {
                assert(cond_ret == ETIMEDOUT);
                GG_LOGW("Timed out waiting for a response.");
                timed_out = true;
                break;
            }
        }
    }

    // timeout handling
    if (timed_out) {
        ggl_client_sub_close(handle);
    }

    GG_LOGD("Finished waiting for a response.");
    return resp_ctx.response_error;
}
