// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_CORE_BUS_AWS_IOT_MQTT_H
#define GGL_CORE_BUS_AWS_IOT_MQTT_H

//! aws_iot_mqtt core-bus interface wrapper

#include <gg/error.h>
#include <gg/types.h>
#include <ggl/core_bus/client.h>
#include <stdbool.h>
#include <stdint.h>

/// Wrapper for core-bus `aws_iot_mqtt` `publish`
/// If `wait_for_resp` is false, uses notify, else call.
GgError ggl_aws_iot_mqtt_publish(
    GgBuffer socket_name,
    GgBuffer topic,
    GgBuffer payload,
    uint8_t qos,
    bool wait_for_resp
);

/// Wrapper for core-bus `aws_iot_mqtt` `subscribe`
GgError ggl_aws_iot_mqtt_subscribe(
    GgBuffer socket_name,
    GgBufList topic_filters,
    uint8_t qos,
    bool virtual,
    GglSubscribeCallback on_response,
    GglSubscribeCloseCallback on_close,
    void *ctx,
    uint32_t *handle
);

/// Parse `aws_iot_mqtt` `subscribe` response data
GgError ggl_aws_iot_mqtt_subscribe_parse_resp(
    GgObject data, GgBuffer *topic, GgBuffer *payload
);

/// Wrapper for core-bus `aws_iot_mqtt` `connection_status`
GgError ggl_aws_iot_mqtt_connection_status(
    GgBuffer socket_name,
    GglSubscribeCallback on_response,
    GglSubscribeCloseCallback on_close,
    void *ctx,
    uint32_t *handle
);

GgError ggl_aws_iot_mqtt_connection_status_parse(
    GgObject data, bool *connection_status
);

#endif
