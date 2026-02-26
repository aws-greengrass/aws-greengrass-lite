// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/flags.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <ggl/core_bus/client.h>
#include <tesd-test.h>
#include <stdint.h>
#include <stdio.h>

GgError run_tesd_test(void) {
    static GgBuffer tesd = GG_STR("aws_iot_tes");

    GgObject result;
    GgMap params = { 0 };
    static uint8_t alloc_buf[4096];
    GgArena alloc = gg_arena_init(GG_BUF(alloc_buf));

    GgError error = ggl_call(
        tesd, GG_STR("request_credentials"), params, NULL, &alloc, &result
    );
    if (error != GG_ERR_OK) {
        return GG_ERR_FAILURE;
    }
    if (gg_obj_type(result) != GG_TYPE_MAP) {
        return GG_ERR_FAILURE;
    }
    GgObject *access_key_id = NULL;
    GgObject *secret_access_key = NULL;
    GgObject *session_token = NULL;
    error = gg_map_validate(
        gg_obj_into_map(result),
        GG_MAP_SCHEMA(
            (GgMapSchemaEntry) { .key = GG_STR("accessKeyId"),
                                 .required = GG_REQUIRED,
                                 .type = GG_TYPE_BUF,
                                 .value = &access_key_id },
            (GgMapSchemaEntry) { .key = GG_STR("secretAccessKey"),
                                 .required = GG_REQUIRED,
                                 .type = GG_TYPE_BUF,
                                 .value = &secret_access_key },
            (GgMapSchemaEntry) { .key = GG_STR("sessionToken"),
                                 .required = GG_REQUIRED,
                                 .type = GG_TYPE_BUF,
                                 .value = &session_token },
        )
    );
    if (error != GG_ERR_OK) {
        return GG_ERR_FAILURE;
    }
    return GG_ERR_OK;
}
