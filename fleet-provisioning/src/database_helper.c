// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "database_helper.h"
#include "ggl/error.h"
#include <ggl/alloc.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

void get_value_from_db(
    GglList key_path, GglAlloc *the_allocator, char *return_string
) {
    GglBuffer config_server = GGL_STR("/aws/ggl/ggconfigd");

    GglMap params = GGL_MAP({ GGL_STR("key_path"), GGL_OBJ(key_path) }, );
    GglObject result;

    GglError error = ggl_call(
        config_server, GGL_STR("read"), params, NULL, the_allocator, &result
    );
    if (error != GGL_ERR_OK) {
        GGL_LOGE("fleet-provisioning", "read failed. Error %d", error);
    } else {
        memcpy(return_string, result.buf.data, result.buf.len);

        if (result.type == GGL_TYPE_BUF) {
            GGL_LOGI(
                "tesd",
                "read value: %.*s",
                (int) result.buf.len,
                (char *) result.buf.data
            );
        }
    }
}

void save_value_to_db(GglList key_path, GglObject value) {
    static uint8_t big_buffer_transfer_for_bump[256] = { 0 };
    GglBuffer ggconfigd = GGL_STR("/aws/ggl/ggconfigd");

    GglBumpAlloc the_allocator
        = ggl_bump_alloc_init(GGL_BUF(big_buffer_transfer_for_bump));

    GglMap params = GGL_MAP(
        { GGL_STR("key_path"), GGL_OBJ(key_path) },
        { GGL_STR("value"), value },
        { GGL_STR("timeStamp"), GGL_OBJ_I64(1723142212) }
    );
    GglObject result;

    GglError error = ggl_call(
        ggconfigd, GGL_STR("write"), params, NULL, &the_allocator.alloc, &result
    );

    if (error != GGL_ERR_OK) {
        GGL_LOGE("fleet-provisioning", "insert failure");
    }
}
