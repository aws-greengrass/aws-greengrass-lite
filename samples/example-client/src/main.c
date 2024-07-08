/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ggl/bump_alloc.h"
#include "ggl/client.h"
#include "ggl/error.h"
#include "ggl/log.h"
#include "ggl/object.h"
#include <errno.h>
#include <time.h>
#include <stddef.h>
#include <stdint.h>

int main(void) {
    GglBuffer server = GGL_STR("/aws/ggl/echo-server");
    static uint8_t buffer[10 * sizeof(GglObject)] = { 0 };

    GglConn *conn;
    GglError ret = ggl_connect(server, &conn);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "client", "Failed to connect to %.*s", (int) server.len, server.data
        );
        return EHOSTUNREACH;
    }

    GglList args = GGL_LIST(GGL_OBJ_STR("hello"), GGL_OBJ_STR("world"));

    struct timespec before;
    struct timespec after;
    clock_gettime(CLOCK_REALTIME, &before);

    for (size_t i = 0; i < 1000000; i++) {
        GglBumpAlloc alloc = ggl_bump_alloc_init(GGL_BUF(buffer));
        GglObject result;

        ret = ggl_call(conn, GGL_STR("echo"), args, &alloc.alloc, &result);

        if (ret != 0) {
            GGL_LOGE("client", "Failed to send echo: %d.", ret);
            break;
        }
    }

    clock_gettime(CLOCK_REALTIME, &after);
    double elapsed_nsecs = (double) (after.tv_sec - before.tv_sec)
        + (double) (after.tv_nsec - before.tv_nsec) / 1000000000;

    GGL_LOGE("client", "Time: %f", elapsed_nsecs);
}
