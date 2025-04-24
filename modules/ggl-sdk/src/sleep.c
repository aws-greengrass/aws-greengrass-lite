// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <errno.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/utils.h>
#include <time.h>
#include <stdint.h>

static GglError sleep_timespec(struct timespec time) {
    struct timespec remain = time;

    while (nanosleep(&remain, &remain) != 0) {
        if (errno != EINTR) {
            GGL_LOGE("nanosleep failed: %d.", errno);
            // TODO: panic instead of returning error.
            return GGL_ERR_FAILURE;
        }
    }

    return GGL_ERR_OK;
}

GglError ggl_sleep(int64_t seconds) {
    struct timespec time = { .tv_sec = seconds };
    return sleep_timespec(time);
}

GglError ggl_sleep_ms(int64_t ms) {
    struct timespec time = { .tv_sec = ms / 1000,
                             .tv_nsec = (int32_t) ((ms % 1000) * 1000 * 1000) };
    return sleep_timespec(time);
}
