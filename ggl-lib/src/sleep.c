// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/error.h"
#include "ggl/log.h"
#include "ggl/utils.h"
#include <errno.h>
#include <time.h>
#include <stdint.h>

GglError ggl_sleep(int64_t seconds) {
    struct timespec time = { .tv_sec = seconds };
    struct timespec remain = { 0 };

    while (nanosleep(&time, &remain) != 0) {
        if (errno != EINTR) {
            int err = errno;
            GGL_LOGE("mqtt", "nanosleep failed: %d.", err);
            return GGL_ERR_FAILURE;
        }

        time = remain;
    }

    return 0;
}
