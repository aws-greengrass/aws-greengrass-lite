// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "bus_server.h"
#include "iotcored.h"
#include "mqtt.h"
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

#define MAXIMUM_CERT_PATH 512
#define MAXIMUM_KEY_PATH 512
#define MAXIMUM_ENDPOINT_LENGTH 128
#define MAXIMUM_THINGNAME_LENGTH 64
#define MAXIMUM_ROOTCA_PATH 512

static GglError collect_a_string(
    GglObject *config_path, char *string, size_t string_length
) {
    GglError error;
    GglObject call_resp;

    GglMap params = GGL_MAP({ GGL_STR("key_path"), *config_path });

    GglBuffer allocator_buffer
        = { .data = (uint8_t *) string, .len = string_length };
    GglBumpAlloc the_allocator = ggl_bump_alloc_init(allocator_buffer);

    GglError ret = ggl_call(
        GGL_STR("/aws/ggl/ggconfigd"),
        GGL_STR("read"),
        params,
        &error,
        &the_allocator.alloc,
        &call_resp
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("run_iotcored", "config read of cert_path failed");
        return GGL_ERR_FATAL;
    }
    if (call_resp.type != GGL_TYPE_BUF) {
        GGL_LOGE(
            "run_iotcored", "expected GGL_BUF for cert path: %d", call_resp.type
        );
        return GGL_ERR_FATAL;
    }
    return GGL_ERR_OK;
}

GglError run_iotcored(IotcoredArgs *args) {
    if (args->cert == NULL) {
        GglObject cert_config_path = GGL_OBJ_LIST(
            GGL_OBJ_STR("system"), GGL_OBJ_STR("certificateFilePath")
        );
        static char cert_memory[MAXIMUM_CERT_PATH + 1] = { 0 };

        GglError error = collect_a_string(
            &cert_config_path, cert_memory, MAXIMUM_CERT_PATH
        );
        if (error != GGL_ERR_OK) {
            return GGL_ERR_FATAL;
        }
        args->cert = cert_memory;
    }

    if (args->endpoint == NULL) {
        GglObject endpoint_config_path = GGL_OBJ_LIST(
            GGL_OBJ_STR("services"),
            GGL_OBJ_STR("aws.greengrass.Nucleus-Lite"),
            GGL_OBJ_STR("configuration"),
            GGL_OBJ_STR("iotDataEndpoint")
        );
        static char endpoint[MAXIMUM_ENDPOINT_LENGTH + 1] = { 0 };
        GglError error = collect_a_string(
            &endpoint_config_path, endpoint, MAXIMUM_ENDPOINT_LENGTH
        );
        if (error != GGL_ERR_OK) {
            return GGL_ERR_FATAL;
        }
        args->cert = endpoint;
    }

    if (args->id == NULL) {
        GglObject id_config_path
            = GGL_OBJ_LIST(GGL_OBJ_STR("system"), GGL_OBJ_STR("thingName"));
        static char thingname[MAXIMUM_THINGNAME_LENGTH + 1] = { 0 };
        GglError error = collect_a_string(
            &id_config_path, thingname, MAXIMUM_THINGNAME_LENGTH
        );
        if (error != GGL_ERR_OK) {
            return GGL_ERR_FATAL;
        }
        args->cert = thingname;
    }

    if (args->key == NULL) {
        GglObject key_config_path = GGL_OBJ_LIST(
            GGL_OBJ_STR("system"), GGL_OBJ_STR("privateKeyPath")
        );
        static char key_path[MAXIMUM_KEY_PATH + 1] = { 0 };
        GglError error
            = collect_a_string(&key_config_path, key_path, MAXIMUM_KEY_PATH);
        if (error != GGL_ERR_OK) {
            return GGL_ERR_FATAL;
        }
        args->cert = key_path;
    }

    if (args->rootca == NULL) {
        GglObject rootca_config_path
            = GGL_OBJ_LIST(GGL_OBJ_STR("system"), GGL_OBJ_STR("rootCaPath"));
        static char rootca_path[MAXIMUM_ROOTCA_PATH + 1] = { 0 };
        GglError error = collect_a_string(
            &rootca_config_path, rootca_path, MAXIMUM_ROOTCA_PATH
        );
        if (error != GGL_ERR_OK) {
            return GGL_ERR_FATAL;
        }
        args->cert = rootca_path;
    }

    GglError ret = iotcored_mqtt_connect(args);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    iotcored_start_server(args);

    return GGL_ERR_FAILURE;
}
