/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IOTCORED_MQTT_H
#define IOTCORED_MQTT_H

#include "args.h"
#include "ggl/error.h"
#include "ggl/object.h"
#include <stdint.h>

typedef struct {
    GglBuffer topic;
    GglBuffer payload;
} IotcoredMsg;

GglError iotcored_mqtt_connect(const IotcoredArgs *args);

GglError iotcored_mqtt_publish(const IotcoredMsg *msg, uint8_t qos);
GglError iotcored_mqtt_subscribe(GglBuffer topic_filter, uint8_t qos);

#endif
