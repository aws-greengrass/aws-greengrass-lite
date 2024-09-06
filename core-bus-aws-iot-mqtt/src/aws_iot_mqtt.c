// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/core_bus/aws_iot_mqtt.h"
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <stdbool.h>
#include <stdint.h>

#define GGL_MQTT_MAX_SUBSCRIBE_FILTERS 10

GglError ggl_aws_iot_mqtt_publish(
    GglBuffer topic, GglBuffer payload, uint8_t qos, bool wait_for_resp
) {
    GglMap args = GGL_MAP(
        { GGL_STR("topic"), GGL_OBJ(topic) },
        { GGL_STR("payload"), GGL_OBJ(payload) },
        { GGL_STR("qos"), GGL_OBJ_I64(qos) }
    );

    if (wait_for_resp) {
        return ggl_call(
            GGL_STR("aws_iot_mqtt"), GGL_STR("publish"), args, NULL, NULL, NULL
        );
    }

    return ggl_notify(GGL_STR("aws_iot_mqtt"), GGL_STR("publish"), args);
}

GglError ggl_aws_iot_mqtt_subscribe(
    GglBuffer *topic_filters,
    size_t count,
    uint8_t qos,
    GglSubscribeCallback on_response,
    GglSubscribeCloseCallback on_close,
    void *ctx,
    uint32_t *handle
) {
    if (count > GGL_MQTT_MAX_SUBSCRIBE_FILTERS) {
        GGL_LOGE("aws_iot_mqtt", "Topic filter count exceeds maximum handled.");
        return GGL_ERR_UNSUPPORTED;
    }

    GglObject filters[GGL_MQTT_MAX_SUBSCRIBE_FILTERS] = { 0 };
    for (size_t i = 0; i < count; i++) {
        filters[i] = GGL_OBJ(topic_filters[i]);
    }

    GglMap args = GGL_MAP(
        { GGL_STR("topic_filter"),
          GGL_OBJ((GglList) { .items = filters, .len = count }) },
        { GGL_STR("qos"), GGL_OBJ_I64(qos) }
    );

    return ggl_subscribe(
        GGL_STR("aws_iot_mqtt"),
        GGL_STR("subscribe"),
        args,
        on_response,
        on_close,
        ctx,
        NULL,
        handle
    );
}

GglError ggl_aws_iot_mqtt_subscribe_parse_resp(
    GglObject data, GglBuffer **topic, GglBuffer **payload
) {
    if (data.type != GGL_TYPE_MAP) {
        GGL_LOGE("aws_iot_mqtt", "Subscription response is not a map.");
        return GGL_ERR_FAILURE;
    }

    GglObject *val;
    if (ggl_map_get(data.map, GGL_STR("topic"), &val)) {
        if (val->type != GGL_TYPE_BUF) {
            GGL_LOGE(
                "aws_iot_mqtt", "Subscription response topic not a buffer."
            );
            return GGL_ERR_FAILURE;
        }
        *topic = &val->buf;
    } else {
        GGL_LOGE("aws_iot_mqtt", "Subscription response is missing topic.");
        return GGL_ERR_FAILURE;
    }

    if (ggl_map_get(data.map, GGL_STR("payload"), &val)) {
        if (val->type != GGL_TYPE_BUF) {
            GGL_LOGE(
                "aws_iot_mqtt", "Subscription response payload not a buffer."
            );
            return GGL_ERR_FAILURE;
        }
        *payload = &val->buf;
    } else {
        GGL_LOGE("aws_iot_mqtt", "Subscription response is missing payload.");
        return GGL_ERR_FAILURE;
    }

    return GGL_ERR_OK;
}
