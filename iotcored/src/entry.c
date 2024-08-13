// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "bus_server.h"
#include "iotcored.h"
#include "mqtt.h"
#include <ggl/error.h>

GglError run_iotcored(IotcoredArgs *args) {
    GglError ret = iotcored_mqtt_connect(args);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    iotcored_start_server(args);

    return GGL_ERR_FAILURE;
}
