// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "bus_server.h"
#include "iotcored.h"
#include "mqtt.h"
#include "subscription_dispatch.h"
#include <ggl/core_bus/server.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

static void rpc_publish(void *ctx, GglMap params, uint32_t handle);
static void rpc_subscribe(void *ctx, GglMap params, uint32_t handle);

void iotcored_start_server(IotcoredArgs *args) {
    GglRpcMethodDesc handlers[] = {
        { GGL_STR("publish"), false, rpc_publish, NULL },
        { GGL_STR("subscribe"), true, rpc_subscribe, NULL },
    };
    size_t handlers_len = sizeof(handlers) / sizeof(handlers[0]);

    GglBuffer interface = GGL_STR("/aws/ggl/iotcored");

    if (args->interface_name != NULL) {
        interface = (GglBuffer) { .data = (uint8_t *) args->interface_name,
                                  .len = strlen(args->interface_name) };
    }
    GglError ret = ggl_listen(interface, handlers, handlers_len);

    GGL_LOGE("iotcored", "Exiting with error %u.", (unsigned) ret);
}

static void rpc_publish(void *ctx, GglMap params, uint32_t handle) {
    (void) ctx;

    GGL_LOGD("rpc-handler", "Handling publish request.");

    IotcoredMsg msg = { 0 };
    uint8_t qos = 0;

    GglObject *val;

    if (ggl_map_get(params, GGL_STR("topic"), &val)
        && (val->type == GGL_TYPE_BUF)) {
        GglBuffer topic = val->buf;
        if (topic.len > UINT16_MAX) {
            GGL_LOGE("rpc-handler", "Publish payload too large.");
            ggl_return_err(handle, GGL_ERR_RANGE);
            return;
        }
        msg.topic = topic;
    } else {
        GGL_LOGE("rpc-handler", "Publish received invalid arguments.");
        ggl_return_err(handle, GGL_ERR_INVALID);
        return;
    }

    if (ggl_map_get(params, GGL_STR("payload"), &val)) {
        if (val->type != GGL_TYPE_BUF) {
            GGL_LOGE("rpc-handler", "Publish received invalid arguments.");
            ggl_return_err(handle, GGL_ERR_INVALID);
            return;
        }
        msg.payload = val->buf;
    }

    if (ggl_map_get(params, GGL_STR("qos"), &val)) {
        if ((val->type != GGL_TYPE_I64) || (val->i64 < 0) || (val->i64 > 2)) {
            GGL_LOGE("rpc-handler", "Publish received invalid arguments.");
            ggl_return_err(handle, GGL_ERR_INVALID);
            return;
        }
        qos = (uint8_t) val->i64;
    }

    GglError ret = iotcored_mqtt_publish(&msg, qos);
    if (ret != GGL_ERR_OK) {
        ggl_return_err(handle, ret);
        return;
    }

    ggl_respond(handle, GGL_OBJ_NULL());
}

static void sub_close_callback(void *ctx, uint32_t handle) {
    (void) ctx;
    iotcored_unregister_subscriptions(handle);
}

static void rpc_subscribe(void *ctx, GglMap params, uint32_t handle) {
    (void) ctx;

    GGL_LOGD("rpc-handler", "Handling subscribe request.");

    static GglBuffer topic_filters[GGL_MQTT_MAX_SUBSCRIBE_FILTERS] = { 0 };
    size_t topic_filter_count = 0;
    uint8_t qos = 0;

    GglObject *val;

    if (!ggl_map_get(params, GGL_STR("topic_filter"), &val)) {
        GGL_LOGE("rpc-handler", "Subscribe received invalid arguments.");
        ggl_return_err(handle, GGL_ERR_INVALID);
        return;
    }

    if (val->type == GGL_TYPE_BUF) {
        topic_filters[0] = val->buf;
        topic_filter_count = 1;
    } else if (val->type == GGL_TYPE_LIST) {
        GglList arg_filters = val->list;
        if (arg_filters.len == 0) {
            GGL_LOGE(
                "rpc-handler", "Subscribe must have at least one topic filter."
            );
            ggl_return_err(handle, GGL_ERR_INVALID);
            return;
        }
        if (arg_filters.len > GGL_MQTT_MAX_SUBSCRIBE_FILTERS) {
            GGL_LOGE(
                "rpc-handler",
                "Subscribe received more topic filters than supported."
            );
            ggl_return_err(handle, GGL_ERR_UNSUPPORTED);
            return;
        }

        topic_filter_count = arg_filters.len;
        for (size_t i = 0; i < arg_filters.len; i++) {
            if (arg_filters.items[i].type != GGL_TYPE_BUF) {
                GGL_LOGE(
                    "rpc-handler", "Subscribe received invalid arguments."
                );
                ggl_return_err(handle, GGL_ERR_INVALID);
                return;
            }
            topic_filters[i] = arg_filters.items[i].buf;
        }
    } else {
        GGL_LOGE("rpc-handler", "Subscribe received invalid arguments.");
        ggl_return_err(handle, GGL_ERR_INVALID);
        return;
    }

    for (size_t i = 0; i < topic_filter_count; i++) {
        if (topic_filters[i].len > UINT16_MAX) {
            GGL_LOGE("rpc-handler", "Topic filter too large.");
            ggl_return_err(handle, GGL_ERR_RANGE);
            return;
        }
    }

    if (ggl_map_get(params, GGL_STR("qos"), &val)) {
        if ((val->type != GGL_TYPE_I64) || (val->i64 < 0) || (val->i64 > 2)) {
            GGL_LOGE("rpc-handler", "Payload received invalid arguments.");
            ggl_return_err(handle, GGL_ERR_INVALID);
            return;
        }
        qos = (uint8_t) val->i64;
    }

    GglError ret = iotcored_register_subscriptions(
        topic_filters, topic_filter_count, handle
    );
    if (ret != GGL_ERR_OK) {
        ggl_return_err(handle, ret);
        return;
    }

    ret = iotcored_mqtt_subscribe(topic_filters, topic_filter_count, qos);
    if (ret != GGL_ERR_OK) {
        iotcored_unregister_subscriptions(handle);
        ggl_return_err(handle, ret);
        return;
    }

    ggl_sub_accept(handle, sub_close_callback, NULL);
}
