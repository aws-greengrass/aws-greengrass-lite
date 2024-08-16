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
#define MAXIMUM_BUFFER_SIZE 512

static GglError collect_a_string(GglList *config_path, GglBuffer *memory) {
    GglMap params = GGL_MAP({ GGL_STR("key_path"), GGL_OBJ(*config_path) });

    uint8_t big_memory[MAXIMUM_BUFFER_SIZE + sizeof(GglObject)] = { 0 };
    GglBumpAlloc the_allocator = ggl_bump_alloc_init(GGL_BUF(big_memory));

    GglObject call_resp;
    GglError ret = ggl_call(
        GGL_STR("/aws/ggl/ggconfigd"),
        GGL_STR("read"),
        params,
        NULL,
        &the_allocator.alloc,
        &call_resp
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("run_iotcored", "Config read of a string failed.");
        return GGL_ERR_FATAL;
    }
    if (call_resp.type != GGL_TYPE_BUF) {
        GGL_LOGE(
            "run_iotcored", "Expected a string, found %d.", call_resp.type
        );
        return GGL_ERR_FATAL;
    }
    if (call_resp.buf.len >= memory->len) {
        GGL_LOGE(
            "run_iotcored",
            "Out of memory.  Needed %zd bytes",
            (call_resp.buf.len - memory->len)
        );
        return GGL_ERR_NOMEM;
    }
    memcpy(memory->data, call_resp.buf.data, call_resp.buf.len);
    memory->len = call_resp.buf.len;
    memory->data[call_resp.buf.len] = '\0';
    return GGL_ERR_OK;
}

GglError run_iotcored(IotcoredArgs *args) {
    if (args->cert == NULL) {
        GglList cert_config_path = GGL_LIST(
            GGL_OBJ_STR("system"), GGL_OBJ_STR("certificateFilePath")
        );
        static uint8_t cert_memory[MAXIMUM_CERT_PATH + 1];
        GglError error
            = collect_a_string(&cert_config_path, &GGL_BUF(cert_memory));
        if (error != GGL_ERR_OK) {
            return GGL_ERR_FATAL;
        }
        args->cert = (char *) cert_memory;
    }

    if (args->endpoint == NULL) {
        GglList endpoint_config_path = GGL_LIST(
            GGL_OBJ_STR("services"),
            GGL_OBJ_STR("aws.greengrass.Nucleus-Lite"),
            GGL_OBJ_STR("configuration"),
            GGL_OBJ_STR("iotDataEndpoint")
        );
        static uint8_t endpoint[MAXIMUM_ENDPOINT_LENGTH + 1];
        GglError error
            = collect_a_string(&endpoint_config_path, &GGL_BUF(endpoint));
        if (error != GGL_ERR_OK) {
            return GGL_ERR_FATAL;
        }
        args->cert = (char *) endpoint;
    }

    if (args->id == NULL) {
        GglList id_config_path
            = GGL_LIST(GGL_OBJ_STR("system"), GGL_OBJ_STR("thingName"));
        static uint8_t thingname[MAXIMUM_THINGNAME_LENGTH + 1];
        GglError error = collect_a_string(&id_config_path, &GGL_BUF(thingname));
        if (error != GGL_ERR_OK) {
            return GGL_ERR_FATAL;
        }
        args->cert = (char *) thingname;
    }

    if (args->key == NULL) {
        GglList key_config_path
            = GGL_LIST(GGL_OBJ_STR("system"), GGL_OBJ_STR("privateKeyPath"));
        static uint8_t key_path[MAXIMUM_KEY_PATH + 1];
        GglError error = collect_a_string(&key_config_path, &GGL_BUF(key_path));
        if (error != GGL_ERR_OK) {
            return GGL_ERR_FATAL;
        }
        args->cert = (char *) key_path;
    }

    if (args->rootca == NULL) {
        GglList rootca_config_path
            = GGL_LIST(GGL_OBJ_STR("system"), GGL_OBJ_STR("rootCaPath"));
        static uint8_t rootca_path[MAXIMUM_ROOTCA_PATH + 1];
        GglError error
            = collect_a_string(&rootca_config_path, &GGL_BUF(rootca_path));
        if (error != GGL_ERR_OK) {
            return GGL_ERR_FATAL;
        }
        args->cert = (char *) rootca_path;
    }

    GglError ret = iotcored_mqtt_connect(args);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    iotcored_start_server(args);

    return GGL_ERR_FAILURE;
}
