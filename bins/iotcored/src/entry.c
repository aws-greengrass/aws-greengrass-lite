// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "bus_server.h"
#include "iotcored.h"
#include "mqtt.h"
#include <ggl/buffer.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/object.h>
#include <limits.h>
#include <string.h>
#include <stdint.h>

#define MAX_ENDPOINT_LEN 128
#define MAX_THINGNAME_LEN 128

GglError run_iotcored(IotcoredArgs *args) {
    if (args->cert == NULL) {
        static uint8_t cert_mem[PATH_MAX] = { 0 };
        GglBuffer cert = GGL_BUF(cert_mem);
        cert.len -= 1;

        GglError ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(GGL_STR("system"), GGL_STR("certificateFilePath")),
            &cert
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        args->cert = (char *) cert_mem;
    }

    if (args->endpoint == NULL) {
        static uint8_t endpoint_mem[MAX_ENDPOINT_LEN + 1] = { 0 };
        GglBuffer endpoint = GGL_BUF(endpoint_mem);
        endpoint.len -= 1;

        GglError ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(
                GGL_STR("services"),
                GGL_STR("aws.greengrass.NucleusLite"),
                GGL_STR("configuration"),
                GGL_STR("iotDataEndpoint")
            ),
            &endpoint
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        args->endpoint = (char *) endpoint_mem;
    }

    if (args->id == NULL) {
        static uint8_t id_mem[MAX_THINGNAME_LEN + 1] = { 0 };
        GglBuffer id = GGL_BUF(id_mem);
        id.len -= 1;

        GglError ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(GGL_STR("system"), GGL_STR("thingName")), &id
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        args->id = (char *) id_mem;
    }

    if (args->key == NULL) {
        static uint8_t key_mem[PATH_MAX] = { 0 };
        GglBuffer key = GGL_BUF(key_mem);
        key.len -= 1;

        GglError ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(GGL_STR("system"), GGL_STR("privateKeyPath")), &key
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        args->key = (char *) key_mem;
    }

    if (args->rootca == NULL) {
        static uint8_t rootca_mem[PATH_MAX] = { 0 };
        GglBuffer rootca = GGL_BUF(rootca_mem);
        rootca.len -= 1;

        GglError ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(GGL_STR("system"), GGL_STR("rootCaPath")), &rootca
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        args->rootca = (char *) rootca_mem;
    }

    GglError ret = iotcored_mqtt_connect(args);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    iotcored_start_server(args);

    return GGL_ERR_FAILURE;
}
