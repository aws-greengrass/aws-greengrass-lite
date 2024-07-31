// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "subscription_dispatch.h"
#include "mqtt.h"
#include <ggl/buffer.h>
#include <ggl/core_bus/server.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <pthread.h>
#include <string.h>
#include <stdint.h>

/// Maximum size of MQTT topic filter supported.
/// Can be configured with `-DIOTCORED_MAX_TOPIC_FILTER_LEN=<N>`.
#ifndef IOTCORED_MAX_TOPIC_FILTER_LEN
#define IOTCORED_MAX_TOPIC_FILTER_LEN 128
#endif

/// Maximum number of MQTT subscriptions supported.
/// Can be configured with `-DIOTCORED_MAX_SUBSCRIPTIONS=<N>`.
#ifndef IOTCORED_MAX_SUBSCRIPTIONS
#define IOTCORED_MAX_SUBSCRIPTIONS 128
#endif

static size_t topic_filter_len[IOTCORED_MAX_SUBSCRIPTIONS] = { 0 };
static uint8_t topic_filters[IOTCORED_MAX_SUBSCRIPTIONS]
                            [IOTCORED_MAX_TOPIC_FILTER_LEN];
static uint32_t handles[IOTCORED_MAX_SUBSCRIPTIONS];
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

static GglBuffer topic_filter_buf(size_t index) {
    return ggl_buffer_substr(
        GGL_BUF(topic_filters[index]), 0, topic_filter_len[index]
    );
}

GglError iotcored_register_subscription(
    GglBuffer topic_filter, uint32_t handle
) {
    if (topic_filter.len == 0) {
        GGL_LOGE(
            "subscriptions", "Attempted to register a 0 length topic filter."
        );
        return GGL_ERR_INVALID;
    }
    if (topic_filter.len > IOTCORED_MAX_TOPIC_FILTER_LEN) {
        GGL_LOGE(
            "subscriptions", "Topic filter larger than configured maximum."
        );
        return GGL_ERR_RANGE;
    }

    pthread_mutex_lock(&mtx);
    GGL_DEFER(pthread_mutex_unlock, mtx);

    for (size_t i = 0; i < IOTCORED_MAX_SUBSCRIPTIONS; i++) {
        if (topic_filter_len[i] == 0) {
            topic_filter_len[i] = topic_filter.len;
            memcpy(topic_filters[i], topic_filter.data, topic_filter.len);
            handles[i] = handle;
            return GGL_ERR_OK;
        }
    }
    GGL_LOGE("subscriptions", "Configured maximum subscriptions exceeded.");
    return GGL_ERR_NOMEM;
}

void iotcored_unregister_subscriptions(uint32_t handle) {
    pthread_mutex_lock(&mtx);
    GGL_DEFER(pthread_mutex_unlock, mtx);

    for (size_t i = 0; i < IOTCORED_MAX_SUBSCRIPTIONS; i++) {
        if (handles[i] == handle) {
            topic_filter_len[i] = 0;
        }
    }
}

void iotcored_mqtt_receive(const IotcoredMsg *msg) {
    pthread_mutex_lock(&mtx);
    GGL_DEFER(pthread_mutex_unlock, mtx);

    for (size_t i = 0; i < IOTCORED_MAX_SUBSCRIPTIONS; i++) {
        if ((topic_filter_len[i] != 0)
            && iotcored_mqtt_topic_filter_match(
                topic_filter_buf(i), msg->topic
            )) {
            ggl_respond(
                handles[i],
                GGL_OBJ_MAP(
                    { GGL_STR("topic"), GGL_OBJ(msg->topic) },
                    { GGL_STR("payload"), GGL_OBJ(msg->payload) }
                )
            );
        }
    }
}
