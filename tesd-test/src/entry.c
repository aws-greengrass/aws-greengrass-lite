// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "tesd-test.h"
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <stdint.h>
#include <stdio.h>

GglError run_tesd_test(void) {
    static GglBuffer tesd = GGL_STR("aws_iot_tes");

    GglObject result;
    GglMap params = { 0 };
    static uint8_t big_buffer_for_bump[4096];
    GglBumpAlloc the_allocator
        = ggl_bump_alloc_init(GGL_BUF(big_buffer_for_bump));

    GglError error = ggl_call(
        tesd,
        GGL_STR("request_credentials"),
        params,
        NULL,
        &the_allocator.alloc,
        &result
    );
    if (error != GGL_ERR_OK) {
        return GGL_ERR_FAILURE;
    }

    GGL_LOGE(
        "Received token, sessionToken: %.*s",
        (int) result.map.pairs[2].val.buf.len,
        result.map.pairs[2].val.buf.data
    );

    return GGL_ERR_OK;
}
