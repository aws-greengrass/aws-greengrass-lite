// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "tesd-test.h"
#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/object.h>
#include <stdint.h>
#include <stdio.h>

GglError run_tesd_test(void) {
    static GglBuffer tesd = GGL_STR("aws_iot_tes");

    GglObject result;
    GglMap params = { 0 };
    static uint8_t alloc_buf[4096];
    GglArena alloc = ggl_arena_init(GGL_BUF(alloc_buf));

    GglError error = ggl_call(
        tesd, GGL_STR("request_credentials"), params, NULL, &alloc, &result
    );
    if (error != GGL_ERR_OK) {
        return GGL_ERR_FAILURE;
    }

    return GGL_ERR_OK;
}
