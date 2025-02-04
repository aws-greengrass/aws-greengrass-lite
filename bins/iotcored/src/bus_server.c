// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "bus_server.h"
#include "iotcored.h"
#include "mqtt.h"
#include "subscription_dispatch.h"
#include <ggl/buffer.h>
#include <ggl/core_bus/server.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

static GglError rpc_publish(void *ctx, GglMap params, uint32_t handle);
static GglError rpc_subscribe(void *ctx, GglMap params, uint32_t handle);
static GglError rpc_get_status(void *ctx, GglMap params, uint32_t handle);

void iotcored_start_server(IotcoredArgs *args) {
    GglRpcMethodDesc handlers[] = {
        { GGL_STR("publish"), false, rpc_publish, NULL },
        { GGL_STR("subscribe"), true, rpc_subscribe, NULL },
        { GGL_STR("connection_status"), true, rpc_get_status, NULL },
    };
    size_t handlers_len = sizeof(handlers) / sizeof(handlers[0]);

    GglBuffer interface = GGL_STR("aws_iot_mqtt");

    if (args->interface_name != NULL) {
        interface = (GglBuffer) { .data = (uint8_t *) args->interface_name,
                                  .len = strlen(args->interface_name) };
    }
    GglError ret = ggl_listen(interface, handlers, handlers_len);

    GGL_LOGE("Exiting with error %u.", (unsigned) ret);
}

static GglError rpc_publish(void *ctx, GglMap params, uint32_t handle) {
    (void) ctx;

    GGL_LOGD("Handling publish request.");

    GglObject *topic_obj;
    GglObject *payload_obj;
    GglObject *qos_obj;
    GglError ret = ggl_map_validate(
        params,
        GGL_MAP_SCHEMA(
            { GGL_STR("topic"), true, GGL_TYPE_BUF, &topic_obj },
            { GGL_STR("payload"), false, GGL_TYPE_BUF, &payload_obj },
            { GGL_STR("qos"), false, GGL_TYPE_I64, &qos_obj },
        )
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Publish received invalid arguments.");
        return GGL_ERR_INVALID;
    }

    IotcoredMsg msg = { .topic = topic_obj->buf, .payload = { 0 } };

    if (msg.topic.len > UINT16_MAX) {
        GGL_LOGE("Publish topic too large.");
        return GGL_ERR_RANGE;
    }

    if (payload_obj != NULL) {
        msg.payload = payload_obj->buf;
    }

    uint8_t qos = 0;

    if (qos_obj != NULL) {
        int64_t qos_val = qos_obj->i64;
        if ((qos_val < 0) || (qos_val > 2)) {
            GGL_LOGE("Publish received QoS out of range.");
            return GGL_ERR_INVALID;
        }
        qos = (uint8_t) qos_val;
    }

    ret = iotcored_mqtt_publish(&msg, qos);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ggl_respond(handle, GGL_OBJ_NULL());
    return GGL_ERR_OK;
}

static void sub_close_callback(void *ctx, uint32_t handle) {
    (void) ctx;
    iotcored_unregister_subscriptions(handle, true);
}

static GglError rpc_subscribe(void *ctx, GglMap params, uint32_t handle) {
    (void) ctx;

    GGL_LOGD("Handling subscribe request.");

    static GglBuffer topic_filters[GGL_MQTT_MAX_SUBSCRIBE_FILTERS] = { 0 };
    size_t topic_filter_count = 0;

    GglObject *val;
    if (!ggl_map_get(params, GGL_STR("topic_filter"), &val)) {
        GGL_LOGE("Subscribe received invalid arguments.");
        return GGL_ERR_INVALID;
    }

    if (val->type == GGL_TYPE_BUF) {
        topic_filters[0] = val->buf;
        topic_filter_count = 1;
    } else if (val->type == GGL_TYPE_LIST) {
        GglList arg_filters = val->list;
        if (arg_filters.len == 0) {
            GGL_LOGE("Subscribe must have at least one topic filter.");
            return GGL_ERR_INVALID;
        }
        if (arg_filters.len > GGL_MQTT_MAX_SUBSCRIBE_FILTERS) {
            GGL_LOGE("Subscribe received more topic filters than supported.");
            return GGL_ERR_UNSUPPORTED;
        }

        topic_filter_count = arg_filters.len;
        for (size_t i = 0; i < arg_filters.len; i++) {
            if (arg_filters.items[i].type != GGL_TYPE_BUF) {
                GGL_LOGE("Subscribe received invalid arguments.");
                return GGL_ERR_INVALID;
            }
            topic_filters[i] = arg_filters.items[i].buf;
        }
    } else {
        GGL_LOGE("Subscribe received invalid arguments.");
        return GGL_ERR_INVALID;
    }

    for (size_t i = 0; i < topic_filter_count; i++) {
        if (topic_filters[i].len > UINT16_MAX) {
            GGL_LOGE("Topic filter too large.");
            return GGL_ERR_RANGE;
        }
    }

    uint8_t qos = 0;
    if (ggl_map_get(params, GGL_STR("qos"), &val)) {
        if ((val->type != GGL_TYPE_I64) || (val->i64 < 0) || (val->i64 > 2)) {
            GGL_LOGE("Payload received invalid arguments.");
            return GGL_ERR_INVALID;
        }
        qos = (uint8_t) val->i64;
    }

    GglError ret = iotcored_register_subscriptions(
        topic_filters, topic_filter_count, handle, qos
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = iotcored_mqtt_subscribe(topic_filters, topic_filter_count, qos);
    if (ret != GGL_ERR_OK) {
        iotcored_unregister_subscriptions(handle, false);
        return ret;
    }

    ggl_sub_accept(handle, sub_close_callback, NULL);
    return GGL_ERR_OK;
}

static void mqtt_status_sub_close_callback(void *ctx, uint32_t handle) {
    (void) ctx;
    iotcored_mqtt_status_update_unregister(handle);
}

static GglError rpc_get_status(void *ctx, GglMap params, uint32_t handle) {
    (void) ctx;
    (void) params;

    GglError ret = iotcored_mqtt_status_update_register(handle);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ggl_sub_accept(handle, mqtt_status_sub_close_callback, NULL);

    // Send a status update as soon as a subscription is accepted.
    iotcored_mqtt_status_update_send(
        GGL_OBJ_BOOL(iotcored_mqtt_connection_status())
    );
    // TODO: have result calculated in status_update send to prevent race
    // condition where status changes after getting it and before sending, and
    // another notification is sent in that window, resulting in out-of-order
    // events.

    return GGL_ERR_OK;
}
