// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "tesd.h"
#include "token_service.h"
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <string.h>
#include <stdint.h>

static void get_value_from_db(
    GglList key_path, GglBumpAlloc the_allocator, char *return_string
) {
    GglBuffer config_server = GGL_STR("gg_config");

    GglMap params = GGL_MAP({ GGL_STR("key_path"), GGL_OBJ(key_path) }, );
    GglObject result;

    GglError error = ggl_call(
        config_server,
        GGL_STR("read"),
        params,
        NULL,
        &the_allocator.alloc,
        &result
    );
    if (error != GGL_ERR_OK) {
        GGL_LOGE("tesd", "read failed. Error %d", error);
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

GglError run_tesd(void) {
    static uint8_t big_buffer_for_bump[3048];
    static char rootca_as_string[512] = { 0 };
    static char cert_path_as_string[512] = { 0 };
    static char key_path_as_string[512] = { 0 };
    static char cert_endpoint_as_string[256] = { 0 };
    static char role_alias_as_string[128] = { 0 };
    static char thing_name_as_string[128] = { 0 };

    GglBumpAlloc the_allocator
        = ggl_bump_alloc_init(GGL_BUF(big_buffer_for_bump));

    // Fetch
    get_value_from_db(
        GGL_LIST(GGL_OBJ_STR("system"), GGL_OBJ_STR("rootCaPath")),
        the_allocator,
        rootca_as_string
    );

    get_value_from_db(
        GGL_LIST(GGL_OBJ_STR("system"), GGL_OBJ_STR("certificateFilePath")),
        the_allocator,
        cert_path_as_string
    );

    get_value_from_db(
        GGL_LIST(GGL_OBJ_STR("system"), GGL_OBJ_STR("privateKeyPath")),
        the_allocator,
        key_path_as_string
    );

    get_value_from_db(
        GGL_LIST(GGL_OBJ_STR("system"), GGL_OBJ_STR("thingName")),
        the_allocator,
        thing_name_as_string
    );

    get_value_from_db(
        GGL_LIST(
            GGL_OBJ_STR("services"),
            GGL_OBJ_STR("aws.greengrass.Nucleus-Lite"),
            GGL_OBJ_STR("configuration"),
            GGL_OBJ_STR("iotRoleAlias")
        ),
        the_allocator,
        role_alias_as_string
    );

    get_value_from_db(
        GGL_LIST(
            GGL_OBJ_STR("services"),
            GGL_OBJ_STR("aws.greengrass.Nucleus-Lite"),
            GGL_OBJ_STR("configuration"),
            GGL_OBJ_STR("iotCredEndpoint")
        ),
        the_allocator,
        cert_endpoint_as_string
    );

    GglError ret = initiate_request(
        rootca_as_string,
        cert_path_as_string,
        key_path_as_string,
        thing_name_as_string,
        role_alias_as_string,
        cert_endpoint_as_string
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return GGL_ERR_FAILURE;
}
