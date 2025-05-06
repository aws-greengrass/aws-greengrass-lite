// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/core_bus/aws_iot_mqtt.h"
#include <ggl/buffer.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/flags.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GGL_MQTT_MAX_SUBSCRIBE_FILTERS 10

GglError ggl_aws_iot_mqtt_publish(
    GglBuffer topic, GglBuffer payload, uint8_t qos, bool wait_for_resp
) {
    GglMap args = GGL_MAP(
        ggl_kv(GGL_STR("topic"), ggl_obj_buf(topic)),
        ggl_kv(GGL_STR("payload"), ggl_obj_buf(payload)),
        ggl_kv(GGL_STR("qos"), ggl_obj_i64(qos))
    );

    if (wait_for_resp) {
        return ggl_call(
            GGL_STR("aws_iot_mqtt"), GGL_STR("publish"), args, NULL, NULL, NULL
        );
    }

    return ggl_notify(GGL_STR("aws_iot_mqtt"), GGL_STR("publish"), args);
}

GglError ggl_aws_iot_mqtt_subscribe(
    GglBufList topic_filters,
    uint8_t qos,
    GglSubscribeCallback on_response,
    GglSubscribeCloseCallback on_close,
    void *ctx,
    uint32_t *handle
) {
    if (topic_filters.len > GGL_MQTT_MAX_SUBSCRIBE_FILTERS) {
        GGL_LOGE("Topic filter count exceeds maximum handled.");
        return GGL_ERR_UNSUPPORTED;
    }

    GglObject filters[GGL_MQTT_MAX_SUBSCRIBE_FILTERS] = { 0 };
    for (size_t i = 0; i < topic_filters.len; i++) {
        filters[i] = ggl_obj_buf(topic_filters.bufs[i]);
    }

    GglMap args = GGL_MAP(
        ggl_kv(
            GGL_STR("topic_filter"),
            ggl_obj_list((GglList) { .items = filters,
                                     .len = topic_filters.len })
        ),
        ggl_kv(GGL_STR("qos"), ggl_obj_i64(qos))
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
    GglObject data, GglBuffer *topic, GglBuffer *payload
) {
    if (ggl_obj_type(data) != GGL_TYPE_MAP) {
        GGL_LOGE("Subscription response is not a map.");
        return GGL_ERR_FAILURE;
    }
    GglMap response = ggl_obj_into_map(data);

    GglObject *topic_obj;
    GglObject *payload_obj;
    GglError ret = ggl_map_validate(
        response,
        GGL_MAP_SCHEMA(
            { GGL_STR("topic"), GGL_REQUIRED, GGL_TYPE_BUF, &topic_obj },
            { GGL_STR("payload"), GGL_REQUIRED, GGL_TYPE_BUF, &payload_obj },
        )
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Received invalid subscription response.");
        return GGL_ERR_FAILURE;
    }

    if (topic != NULL) {
        *topic = ggl_obj_into_buf(*topic_obj);
    }

    if (payload != NULL) {
        *payload = ggl_obj_into_buf(*payload_obj);
    }

    return GGL_ERR_OK;
}

/// Call this API to subscribe to MQTT connection status. To parse the data
/// received from the subscription, call
/// ggl_aws_iot_mqtt_connection_status_parse function which will return a true
/// for connected and a false for not connected.
///
/// Note that when a subscription is accepted, the current MQTT status is sent
/// to the subscribers.
GglError ggl_aws_iot_mqtt_connection_status(
    GglSubscribeCallback on_response,
    GglSubscribeCloseCallback on_close,
    void *ctx,
    uint32_t *handle
) {
    // The GGL subscribe API expects a map. Sending a dummy map.
    GglMap args = GGL_MAP();
    return ggl_subscribe(
        GGL_STR("aws_iot_mqtt"),
        GGL_STR("connection_status"),
        args,
        on_response,
        on_close,
        ctx,
        NULL,
        handle
    );
}

GglError ggl_aws_iot_mqtt_connection_status_parse(
    GglObject data, bool *connection_status
) {
    if (ggl_obj_type(data) != GGL_TYPE_BOOLEAN) {
        GGL_LOGE(
            "MQTT connection status subscription response is not a boolean."
        );
        return GGL_ERR_FAILURE;
    }

    *connection_status = ggl_obj_into_bool(data);

    return GGL_ERR_OK;
}
