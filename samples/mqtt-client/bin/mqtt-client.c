// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <errno.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/aws_iot_mqtt.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/utils.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static GglError subscribe_callback(void *ctx, uint32_t handle, GglObject data) {
    (void) ctx;
    (void) handle;

    GglBuffer *topic;
    GglBuffer *payload;

    GglError ret
        = ggl_aws_iot_mqtt_subscribe_parse_resp(data, &topic, &payload);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GGL_LOGI(
        "mqtt-client",
        "Got message from IoT Core; topic: %.*s, payload: %.*s.",
        (int) topic->len,
        topic->data,
        (int) payload->len,
        payload->data
    );

    return GGL_ERR_OK;
}

int main(void) {
    GglError ret = ggl_aws_iot_mqtt_subscribe(
        GGL_BUF_LIST(GGL_STR("hello")), 0, subscribe_callback, NULL, NULL, NULL
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("mqtt-client", "Failed to send subscription");
        return EPROTO;
    }
    GGL_LOGI("mqtt-client", "Successfully sent subscription.");

    ggl_sleep(1);

    ret = ggl_aws_iot_mqtt_publish(
        GGL_STR("hello"), GGL_STR("hello world"), 0, false
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("mqtt-client", "Failed to send publish.");
        return EPROTO;
    }
    GGL_LOGI("mqtt-client", "Sent MQTT publish.");

    ggl_sleep(5);
}
