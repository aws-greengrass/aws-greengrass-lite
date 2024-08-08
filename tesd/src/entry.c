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

static void get_value_from_db(
    GglBuffer component,
    GglBuffer test_key,
    GglBumpAlloc the_allocator,
    char *return_string
);

void get_value_from_db(
    GglBuffer component,
    GglBuffer test_key,
    GglBumpAlloc the_allocator,
    char *return_string
) {
    GglBuffer config_server = GGL_STR("/aws/ggl/ggconfigd");

    GglMap params = GGL_MAP(
        { GGL_STR("component"), GGL_OBJ(component) },
        { GGL_STR("key"), GGL_OBJ(test_key) },
    );
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
        GGL_LOGE(
            "tesd",
            "%.*s read failed. Error %d",
            (int) component.len,
            component.data,
            error
        );
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

    // test_insert(
    //     GGL_STR("system"),
    //     GGL_STR("rootCaPath"),
    //     GGL_STR("/home/ubuntu/repo/Creds/fleetClaim/AmazonRootCA1.pem")
    // );

    // test_insert(
    //     GGL_STR("system"),
    //     GGL_STR("privateKeyPath"),
    //     GGL_STR("/home/ubuntu/repo/Creds/rawalGammaDevice/private.pem.key")
    // );

    // test_insert(
    //     GGL_STR("system"),
    //     GGL_STR("certificateFilePath"),
    //     GGL_STR("/home/ubuntu/repo/Creds/rawalGammaDevice/certificate.pem.crt")
    // );

    // test_insert(
    //     GGL_STR("system"), GGL_STR("thingName"), GGL_STR("rawalGammaDevice")
    // );
    // test_insert(
    //     GGL_STR("nucleus"),
    //     GGL_STR("configuration/iotRoleAlias"),
    //     GGL_STR("GreengrassV2GammaTokenExchangeRoleAlias")
    // );

    // test_insert(
    //     GGL_STR("nucleus"),
    //     GGL_STR("configuration/iotCredEndpoint"),
    //     GGL_STR("c2fj26n8iep3iy.credentials.gamma.us-east-1.iot.amazonaws.com")
    // );

    // Fetch
    get_value_from_db(
        GGL_STR("system"),
        GGL_STR("rootCaPath"),
        the_allocator,
        rootca_as_string
    );

    get_value_from_db(
        GGL_STR("system"),
        GGL_STR("certificateFilePath"),
        the_allocator,
        cert_path_as_string
    );

    get_value_from_db(
        GGL_STR("system"),
        GGL_STR("privateKeyPath"),
        the_allocator,
        key_path_as_string
    );

    get_value_from_db(
        GGL_STR("system"),
        GGL_STR("thingName"),
        the_allocator,
        thing_name_as_string
    );

    get_value_from_db(
        GGL_STR("nucleus"),
        GGL_STR("configuration/iotRoleAlias"),
        the_allocator,
        role_alias_as_string
    );

    get_value_from_db(
        GGL_STR("nucleus"),
        GGL_STR("configuration/iotCredEndpoint"),
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

    // iotcored_start_server(args);

    return GGL_ERR_FAILURE;
}
