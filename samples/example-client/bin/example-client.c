// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <errno.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <time.h>
#include <stdint.h>

int main(void) {
    GglBuffer server = GGL_STR("echo_server");
    static uint8_t buffer[10 * sizeof(GglObject)] = { 0 };

    GglMap args
        = GGL_MAP({ GGL_STR("message"), GGL_OBJ_BUF(GGL_STR("hello world")) });

    struct timespec before;
    struct timespec after;
    clock_gettime(CLOCK_REALTIME, &before);

    for (size_t i = 0; i < 1000000; i++) {
        GglBumpAlloc alloc = ggl_bump_alloc_init(GGL_BUF(buffer));
        GglObject result;

        GglError ret = ggl_call(
            server, GGL_STR("echo"), args, NULL, &alloc.alloc, &result
        );

        if (ret != 0) {
            GGL_LOGE("Failed to send echo: %d.", ret);
            return EPROTO;
        }
    }

    clock_gettime(CLOCK_REALTIME, &after);
    double elapsed_nsecs = (double) (after.tv_sec - before.tv_sec)
        + (double) (after.tv_nsec - before.tv_nsec) / 1000000000;

    GGL_LOGE("Time: %f", elapsed_nsecs);
}
