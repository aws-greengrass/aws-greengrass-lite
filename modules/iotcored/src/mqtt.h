// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef IOTCORED_MQTT_H
#define IOTCORED_MQTT_H

#include <gg/error.h>
#include <gg/types.h>
#include <iotcored.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/// Maximum number of topic filters supported in a subscription request
#define GGL_MQTT_MAX_SUBSCRIBE_FILTERS 10

typedef struct {
    GgBuffer topic;
    GgBuffer payload;
} IotcoredMsg;

GgError iotcored_mqtt_connect(const IotcoredArgs *args);

bool iotcored_mqtt_connection_status(void);

GgError iotcored_mqtt_publish(const IotcoredMsg *msg, uint8_t qos);
GgError iotcored_mqtt_subscribe(
    GgBuffer *topic_filters, size_t count, uint8_t qos
);
GgError iotcored_mqtt_unsubscribe(GgBuffer *topic_filters, size_t count);

bool iotcored_mqtt_topic_filter_match(GgBuffer topic_filter, GgBuffer topic);

void iotcored_mqtt_receive(const IotcoredMsg *msg);

#endif
