// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/flags.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <gg/utils.h>
#include <ggl/core_bus/client.h>
#include <ggl/core_bus/gg_config.h>
#include <tesd-test.h>
#include <stddef.h>
#include <stdint.h>

static GgError test_request_credentials(void) {
    GG_LOGI("Test: request_credentials returns valid response");

    static GgBuffer tesd = GG_STR("aws_iot_tes");
    GgObject result;
    GgMap params = { 0 };
    static uint8_t alloc_buf[4096];
    GgArena alloc = gg_arena_init(GG_BUF(alloc_buf));

    GgError error = ggl_call(
        tesd, GG_STR("request_credentials"), params, NULL, &alloc, &result
    );
    if (error != GG_ERR_OK) {
        GG_LOGE("request_credentials failed: %d", error);
        return GG_ERR_FAILURE;
    }
    if (gg_obj_type(result) != GG_TYPE_MAP) {
        GG_LOGE("request_credentials result is not a map");
        return GG_ERR_FAILURE;
    }

    GgObject *access_key_id = NULL;
    GgObject *secret_access_key = NULL;
    GgObject *session_token = NULL;
    error = gg_map_validate(
        gg_obj_into_map(result),
        GG_MAP_SCHEMA(
            { GG_STR("accessKeyId"), GG_REQUIRED, GG_TYPE_BUF, &access_key_id },
            { GG_STR("secretAccessKey"),
              GG_REQUIRED,
              GG_TYPE_BUF,
              &secret_access_key },
            { GG_STR("sessionToken"),
              GG_REQUIRED,
              GG_TYPE_BUF,
              &session_token },
        )
    );
    if (error != GG_ERR_OK) {
        GG_LOGE("request_credentials response missing required fields");
        return GG_ERR_FAILURE;
    }

    GG_LOGI("PASSED: request_credentials");
    return GG_ERR_OK;
}

static GgError test_endpoint_change_keeps_tesd_responsive(void) {
    GG_LOGI("Test: tesd remains responsive after iotCredEndpoint change");

    // Read current endpoint
    static uint8_t orig_ep_mem[128] = { 0 };
    GgArena alloc = gg_arena_init(GG_BUF(orig_ep_mem));
    GgBuffer orig_ep;
    GgError ret = ggl_gg_config_read_str(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("aws.greengrass.NucleusLite"),
            GG_STR("configuration"),
            GG_STR("iotCredEndpoint")
        ),
        &alloc,
        &orig_ep
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to read current iotCredEndpoint: %d", ret);
        return ret;
    }

    // Write a different endpoint to trigger subscription callback
    ret = ggl_gg_config_write(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("aws.greengrass.NucleusLite"),
            GG_STR("configuration"),
            GG_STR("iotCredEndpoint")
        ),
        gg_obj_buf(
            GG_STR("test-endpoint.credentials.iot.us-east-1.amazonaws.com")
        ),
        NULL
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to write test iotCredEndpoint: %d", ret);
        return ret;
    }

    // Give tesd time to process the subscription callback
    (void) gg_sleep(1);

    // Verify tesd is still responsive (credential fetch will fail since
    // test-endpoint is not real, but the RPC call itself should succeed)
    static GgBuffer tesd = GG_STR("aws_iot_tes");
    GgObject result;
    GgMap params = { 0 };
    static uint8_t resp_buf[4096];
    GgArena resp_alloc = gg_arena_init(GG_BUF(resp_buf));

    // This will fail with a network error (fake endpoint), which is expected.
    // We just verify tesd didn't crash from the config change.
    (void) ggl_call(
        tesd, GG_STR("request_credentials"), params, NULL, &resp_alloc, &result
    );

    // Restore original endpoint
    ret = ggl_gg_config_write(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("aws.greengrass.NucleusLite"),
            GG_STR("configuration"),
            GG_STR("iotCredEndpoint")
        ),
        gg_obj_buf(orig_ep),
        NULL
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to restore original iotCredEndpoint: %d", ret);
        return ret;
    }

    // Give tesd time to switch back
    (void) gg_sleep(1);

    // Verify tesd works with restored endpoint
    ret = test_request_credentials();
    if (ret != GG_ERR_OK) {
        GG_LOGE("tesd not responsive after restoring endpoint");
        return ret;
    }

    GG_LOGI("PASSED: endpoint_change_keeps_tesd_responsive");
    return GG_ERR_OK;
}

GgError run_tesd_test(void) {
    GgError ret = test_request_credentials();
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ret = test_endpoint_change_keeps_tesd_responsive();
    if (ret != GG_ERR_OK) {
        return ret;
    }

    GG_LOGI("All tesd tests passed.");
    return GG_ERR_OK;
}
