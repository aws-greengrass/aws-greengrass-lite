// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "bus_server.h"
#include "mqtt.h"
#include "subscription_dispatch.h"
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/flags.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <ggl/core_bus/server.h>
#include <iotcored.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

static GgError rpc_publish(void *ctx, GgMap params, uint32_t handle);
static GgError rpc_subscribe(void *ctx, GgMap params, uint32_t handle);
static GgError rpc_get_status(void *ctx, GgMap params, uint32_t handle);

void iotcored_start_server(IotcoredArgs *args) {
    GglRpcMethodDesc handlers[] = {
        { GG_STR("publish"), false, rpc_publish, NULL },
        { GG_STR("subscribe"), true, rpc_subscribe, NULL },
        { GG_STR("connection_status"), true, rpc_get_status, NULL },
    };
    size_t handlers_len = sizeof(handlers) / sizeof(handlers[0]);

    GgBuffer interface = GG_STR("aws_iot_mqtt");

    if (args->interface_name != NULL) {
        interface = (GgBuffer) { .data = (uint8_t *) args->interface_name,
                                 .len = strlen(args->interface_name) };
    }
    GgError ret = ggl_listen(interface, handlers, handlers_len);

    GG_LOGE("Exiting with error %u.", (unsigned) ret);
}

static GgError rpc_publish(void *ctx, GgMap params, uint32_t handle) {
    (void) ctx;

    GG_LOGD("Handling publish request.");

    GgObject *topic_obj;
    GgObject *payload_obj;
    GgObject *qos_obj;
    GgError ret = gg_map_validate(
        params,
        GG_MAP_SCHEMA(
            { GG_STR("topic"), GG_REQUIRED, GG_TYPE_BUF, &topic_obj },
            { GG_STR("payload"), GG_OPTIONAL, GG_TYPE_BUF, &payload_obj },
            { GG_STR("qos"), GG_OPTIONAL, GG_TYPE_I64, &qos_obj },
        )
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Publish received invalid arguments.");
        return GG_ERR_INVALID;
    }

    IotcoredMsg msg
        = { .topic = gg_obj_into_buf(*topic_obj), .payload = { 0 } };

    if (msg.topic.len > UINT16_MAX) {
        GG_LOGE("Publish topic too large.");
        return GG_ERR_RANGE;
    }

    if (payload_obj != NULL) {
        msg.payload = gg_obj_into_buf(*payload_obj);
    }

    uint8_t qos = 0;

    if (qos_obj != NULL) {
        int64_t qos_val = gg_obj_into_i64(*qos_obj);
        if ((qos_val < 0) || (qos_val > 2)) {
            GG_LOGE("Publish received QoS out of range.");
            return GG_ERR_INVALID;
        }
        qos = (uint8_t) qos_val;
    }

    ret = iotcored_mqtt_publish(&msg, qos);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ggl_respond(handle, GG_OBJ_NULL);
    return GG_ERR_OK;
}

static void sub_close_callback(void *ctx, uint32_t handle) {
    (void) ctx;
    iotcored_unregister_subscriptions(handle, true);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static GgError rpc_subscribe(void *ctx, GgMap params, uint32_t handle) {
    (void) ctx;

    GG_LOGD("Handling subscribe request.");

    static GgBuffer topic_filters[GGL_MQTT_MAX_SUBSCRIBE_FILTERS] = { 0 };
    size_t topic_filter_count = 0;

    GgObject *val;
    if (!gg_map_get(params, GG_STR("topic_filter"), &val)) {
        GG_LOGE("Subscribe received invalid arguments.");
        return GG_ERR_INVALID;
    }

    if (gg_obj_type(*val) == GG_TYPE_BUF) {
        topic_filters[0] = gg_obj_into_buf(*val);
        topic_filter_count = 1;
    } else if (gg_obj_type(*val) == GG_TYPE_LIST) {
        GgList arg_filters = gg_obj_into_list(*val);
        if (arg_filters.len == 0) {
            GG_LOGE("Subscribe must have at least one topic filter.");
            return GG_ERR_INVALID;
        }
        if (arg_filters.len > GGL_MQTT_MAX_SUBSCRIBE_FILTERS) {
            GG_LOGE("Subscribe received more topic filters than supported.");
            return GG_ERR_UNSUPPORTED;
        }

        topic_filter_count = arg_filters.len;
        for (size_t i = 0; i < arg_filters.len; i++) {
            if (gg_obj_type(arg_filters.items[i]) != GG_TYPE_BUF) {
                GG_LOGE("Subscribe received invalid arguments.");
                return GG_ERR_INVALID;
            }
            topic_filters[i] = gg_obj_into_buf(arg_filters.items[i]);
        }
    } else {
        GG_LOGE("Subscribe received invalid arguments.");
        return GG_ERR_INVALID;
    }

    bool virtual = false;
    if (gg_map_get(params, GG_STR("virtual"), &val)) {
        virtual = gg_obj_into_bool(*val);
    }

    for (size_t i = 0; i < topic_filter_count; i++) {
        if (topic_filters[i].len > UINT16_MAX) {
            GG_LOGE("Topic filter too large.");
            return GG_ERR_RANGE;
        }
    }

    uint8_t qos = 0;
    if (gg_map_get(params, GG_STR("qos"), &val)) {
        if (gg_obj_type(*val) != GG_TYPE_I64) {
            GG_LOGE("Subscribe received invalid arguments.");
            return GG_ERR_INVALID;
        }
        int64_t qos_val = gg_obj_into_i64(*val);
        if ((qos_val < 0) || (qos_val > 2)) {
            GG_LOGE("Subscribe received invalid arguments.");
            return GG_ERR_INVALID;
        }
        qos = (uint8_t) qos_val;
    }

    GgError ret = iotcored_register_subscriptions(
        topic_filters, topic_filter_count, handle, qos
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    if (!virtual) {
        ret = iotcored_mqtt_subscribe(topic_filters, topic_filter_count, qos);
        if (ret != GG_ERR_OK) {
            iotcored_unregister_subscriptions(handle, false);
            return ret;
        }
    }

    ggl_sub_accept(handle, sub_close_callback, NULL);
    return GG_ERR_OK;
}

static void mqtt_status_sub_close_callback(void *ctx, uint32_t handle) {
    (void) ctx;
    iotcored_mqtt_status_update_unregister(handle);
}

static GgError rpc_get_status(void *ctx, GgMap params, uint32_t handle) {
    (void) ctx;
    (void) params;

    GgError ret = iotcored_mqtt_status_update_register(handle);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ggl_sub_accept(handle, mqtt_status_sub_close_callback, NULL);

    // Send a status update as soon as a subscription is accepted.
    iotcored_mqtt_status_update_send(
        gg_obj_bool(iotcored_mqtt_connection_status())
    );
    // TODO: have result calculated in status_update send to prevent race
    // condition where status changes after getting it and before sending, and
    // another notification is sent in that window, resulting in out-of-order
    // events.

    return GG_ERR_OK;
}
