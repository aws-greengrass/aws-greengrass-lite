// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/backoff.h"
#include "stdlib.h"
#include <assert.h>
#include <backoff_algorithm.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/rand.h>
#include <ggl/utils.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static GglError backoff_wrapper(
    uint32_t base_ms,
    uint32_t max_ms,
    uint32_t max_attempts,
    GglError (*fn)(void *ctx),
    void *ctx
) {
    if (fn == NULL) {
        assert(false);
        return GGL_ERR_UNSUPPORTED;
    }

    if (base_ms > UINT16_MAX) {
        assert(false);
        return GGL_ERR_UNSUPPORTED;
    }

    if (max_ms > UINT16_MAX) {
        assert(false);
        return GGL_ERR_UNSUPPORTED;
    }

    BackoffAlgorithmContext_t retry_params;
    BackoffAlgorithm_InitializeParams(
        &retry_params, (uint16_t) base_ms, (uint16_t) max_ms, max_attempts
    );

    BackoffAlgorithmStatus_t retry_status = BackoffAlgorithmSuccess;

    while (true) {
        GglError ret = fn(ctx);

        if (ret == GGL_ERR_OK) {
            return GGL_ERR_OK;
        }

        uint32_t rand_val;
        GglError rand_err = ggl_rand_fill((GglBuffer
        ) { .data = (uint8_t *) &rand_val, .len = sizeof(rand_val) });
        if (rand_err != GGL_ERR_OK) {
            // TODO: call proper panic function
            GGL_LOGE("Fatal error: could not get random value during backoff.");
            _Exit(1);
        }

        uint16_t backoff_time = 0;
        retry_status = BackoffAlgorithm_GetNextBackoff(
            &retry_params, rand_val, &backoff_time
        );

        if (retry_status == BackoffAlgorithmRetriesExhausted) {
            return ret;
        }

        GglError sleep_err = ggl_sleep_ms(backoff_time);
        if (sleep_err != GGL_ERR_OK) {
            // TODO: call proper panic function
            GGL_LOGE("Fatal error: unexpected sleep error during backoff.");
            _Exit(1);
        }
    }

    assert(false);
    return GGL_ERR_FATAL;
}

GglError ggl_backoff(
    uint32_t base_ms,
    uint32_t max_ms,
    uint32_t max_attempts,
    GglError (*fn)(void *ctx),
    void *ctx
) {
    assert(max_attempts != BACKOFF_ALGORITHM_RETRY_FOREVER);
    return backoff_wrapper(base_ms, max_ms, max_attempts, fn, ctx);
}

void ggl_backoff_indefinite(
    uint32_t base_ms, uint32_t max_ms, GglError (*fn)(void *ctx), void *ctx
) {
    GglError ret = backoff_wrapper(
        base_ms, max_ms, BACKOFF_ALGORITHM_RETRY_FOREVER, fn, ctx
    );
    // TODO: Perhaps should panic/log/etc.
    assert(ret == GGL_ERR_OK);
}
