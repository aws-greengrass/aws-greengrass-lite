// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/flags.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <ggl/core_bus/aws_iot_mqtt.h>
#include <ggl/core_bus/client.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GGL_MQTT_MAX_SUBSCRIBE_FILTERS 10

GgError ggl_aws_iot_mqtt_publish(
    GgBuffer socket_name,
    GgBuffer topic,
    GgBuffer payload,
    uint8_t qos,
    bool wait_for_resp
) {
    GgMap args = GG_MAP(
        gg_kv(GG_STR("topic"), gg_obj_buf(topic)),
        gg_kv(GG_STR("payload"), gg_obj_buf(payload)),
        gg_kv(GG_STR("qos"), gg_obj_i64(qos))
    );

    if (wait_for_resp) {
        return ggl_call(socket_name, GG_STR("publish"), args, NULL, NULL, NULL);
    }

    return ggl_notify(socket_name, GG_STR("publish"), args);
}

GgError ggl_aws_iot_mqtt_subscribe(
    GgBuffer socket_name,
    GgBufList topic_filters,
    uint8_t qos,
    bool virtual,
    GglSubscribeCallback on_response,
    GglSubscribeCloseCallback on_close,
    void *ctx,
    uint32_t *handle
) {
    if (topic_filters.len > GGL_MQTT_MAX_SUBSCRIBE_FILTERS) {
        GG_LOGE("Topic filter count exceeds maximum handled.");
        return GG_ERR_UNSUPPORTED;
    }

    GgObject filters[GGL_MQTT_MAX_SUBSCRIBE_FILTERS] = { 0 };
    for (size_t i = 0; i < topic_filters.len; i++) {
        filters[i] = gg_obj_buf(topic_filters.bufs[i]);
    }

    GgMap args = GG_MAP(
        gg_kv(
            GG_STR("topic_filter"),
            gg_obj_list((GgList) { .items = filters, .len = topic_filters.len })
        ),
        gg_kv(GG_STR("qos"), gg_obj_i64(qos)),
        gg_kv(GG_STR("virtual"), gg_obj_bool(virtual))
    );

    return ggl_subscribe(
        socket_name,
        GG_STR("subscribe"),
        args,
        on_response,
        on_close,
        ctx,
        NULL,
        handle
    );
}

GgError ggl_aws_iot_mqtt_subscribe_parse_resp(
    GgObject data, GgBuffer *topic, GgBuffer *payload
) {
    if (gg_obj_type(data) != GG_TYPE_MAP) {
        GG_LOGE("Subscription response is not a map.");
        return GG_ERR_FAILURE;
    }
    GgMap response = gg_obj_into_map(data);

    GgObject *topic_obj;
    GgObject *payload_obj;
    GgError ret = gg_map_validate(
        response,
        GG_MAP_SCHEMA(
            { GG_STR("topic"), GG_REQUIRED, GG_TYPE_BUF, &topic_obj },
            { GG_STR("payload"), GG_REQUIRED, GG_TYPE_BUF, &payload_obj },
        )
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Received invalid subscription response.");
        return GG_ERR_FAILURE;
    }

    if (topic != NULL) {
        *topic = gg_obj_into_buf(*topic_obj);
    }

    if (payload != NULL) {
        *payload = gg_obj_into_buf(*payload_obj);
    }

    return GG_ERR_OK;
}

/// Call this API to subscribe to MQTT connection status. To parse the data
/// received from the subscription, call
/// ggl_aws_iot_mqtt_connection_status_parse function which will return a true
/// for connected and a false for not connected.
///
/// Note that when a subscription is accepted, the current MQTT status is sent
/// to the subscribers.
GgError ggl_aws_iot_mqtt_connection_status(
    GgBuffer socket_name,
    GglSubscribeCallback on_response,
    GglSubscribeCloseCallback on_close,
    void *ctx,
    uint32_t *handle
) {
    // The GGL subscribe API expects a map. Sending a dummy map.
    GgMap args = GG_MAP();
    return ggl_subscribe(
        socket_name,
        GG_STR("connection_status"),
        args,
        on_response,
        on_close,
        ctx,
        NULL,
        handle
    );
}

GgError ggl_aws_iot_mqtt_connection_status_parse(
    GgObject data, bool *connection_status
) {
    if (gg_obj_type(data) != GG_TYPE_BOOLEAN) {
        GG_LOGE("MQTT connection status subscription response is not a boolean."
        );
        return GG_ERR_FAILURE;
    }

    *connection_status = gg_obj_into_bool(data);

    return GG_ERR_OK;
}
