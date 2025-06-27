// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "tesd-test.h"
#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/flags.h>
#include <ggl/map.h>
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
    if (ggl_obj_type(result) != GGL_TYPE_MAP) {
        return GGL_ERR_FAILURE;
    }
    GglObject *access_key_id = NULL;
    GglObject *secret_access_key = NULL;
    GglObject *session_token = NULL;
    error = ggl_map_validate(
        ggl_obj_into_map(result),
        GGL_MAP_SCHEMA(
            (GglMapSchemaEntry) { .key = GGL_STR("accessKeyId"),
                                  .required = GGL_REQUIRED,
                                  .type = GGL_TYPE_BUF,
                                  .value = &access_key_id },
            (GglMapSchemaEntry) { .key = GGL_STR("secretAccessKey"),
                                  .required = GGL_REQUIRED,
                                  .type = GGL_TYPE_BUF,
                                  .value = &secret_access_key },
            (GglMapSchemaEntry) { .key = GGL_STR("sessionToken"),
                                  .required = GGL_REQUIRED,
                                  .type = GGL_TYPE_BUF,
                                  .value = &session_token },
        )
    );
    if (error != GGL_ERR_OK) {
        return GGL_ERR_FAILURE;
    }
    return GGL_ERR_OK;
}
