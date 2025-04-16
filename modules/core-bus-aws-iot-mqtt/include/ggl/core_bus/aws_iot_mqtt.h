// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_CORE_BUS_AWS_IOT_MQTT_H
#define GGL_CORE_BUS_AWS_IOT_MQTT_H

//! aws_iot_mqtt core-bus interface wrapper

#include <ggl/buffer.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/object.h>
#include <stdbool.h>
#include <stdint.h>

/// Wrapper for core-bus `aws_iot_mqtt` `publish`
/// If `wait_for_resp` is false, uses notify, else call.
GglError ggl_aws_iot_mqtt_publish(
    GglBuffer topic, GglBuffer payload, uint8_t qos, bool wait_for_resp
);

/// Wrapper for core-bus `aws_iot_mqtt` `subscribe`
GglError ggl_aws_iot_mqtt_subscribe(
    GglBufList topic_filters,
    uint8_t qos,
    GglSubscribeCallback on_response,
    GglSubscribeCloseCallback on_close,
    void *ctx,
    uint32_t *handle
);

/// Parse `aws_iot_mqtt` `subscribe` response data
GglError ggl_aws_iot_mqtt_subscribe_parse_resp(
    GglObject data, GglBuffer *topic, GglBuffer *payload
);

/// Wrapper for core-bus `aws_iot_mqtt` `connection_status`
GglError ggl_aws_iot_mqtt_connection_status(
    GglSubscribeCallback on_response,
    GglSubscribeCloseCallback on_close,
    void *ctx,
    uint32_t *handle
);

GglError ggl_aws_iot_mqtt_connection_status_parse(
    GglObject data, bool *connection_status
);

#endif
