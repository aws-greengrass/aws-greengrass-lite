// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "subscription_dispatch.h"
#include "mqtt.h"
#include <sys/types.h>
#include <ggl/buffer.h>
#include <ggl/cleanup.h>
#include <ggl/core_bus/server.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <pthread.h>
#include <string.h>
#include <stdint.h>

/// Maximum size of MQTT topic for AWS IoT.
/// Basic ingest topics can be longer but can't be subscribed to.
/// This is a limit for topic lengths that we may receive publishes on.
/// https://docs.aws.amazon.com/general/latest/gr/iot-core.html#limits_iot
#define AWS_IOT_MAX_TOPIC_SIZE 256

/// Maximum number of MQTT subscriptions supported.
/// Can be configured with `-DIOTCORED_MAX_SUBSCRIPTIONS=<N>`.
#ifndef IOTCORED_MAX_SUBSCRIPTIONS
#define IOTCORED_MAX_SUBSCRIPTIONS 128
#endif

static size_t topic_filter_len[IOTCORED_MAX_SUBSCRIPTIONS] = { 0 };
static uint8_t sub_topic_filters[IOTCORED_MAX_SUBSCRIPTIONS]
                                [AWS_IOT_MAX_TOPIC_SIZE];
static uint32_t handles[IOTCORED_MAX_SUBSCRIPTIONS];
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

static GglBuffer topic_filter_buf(size_t index) {
    return ggl_buffer_substr(
        GGL_BUF(sub_topic_filters[index]), 0, topic_filter_len[index]
    );
}

GglError iotcored_register_subscriptions(
    GglBuffer *topic_filters, size_t count, uint32_t handle
) {
    for (size_t i = 0; i < count; i++) {
        if (topic_filters[i].len == 0) {
            GGL_LOGE("Attempted to register a 0 length topic filter.");
            return GGL_ERR_INVALID;
        }
    }
    for (size_t i = 0; i < count; i++) {
        if (topic_filters[i].len > AWS_IOT_MAX_TOPIC_SIZE) {
            GGL_LOGE("Topic filter exceeds max length.");
            return GGL_ERR_RANGE;
        }
    }

    GGL_MTX_SCOPE_GUARD(&mtx);

    size_t filter_index = 0;
    for (size_t i = 0; i < IOTCORED_MAX_SUBSCRIPTIONS; i++) {
        if (topic_filter_len[i] == 0) {
            topic_filter_len[i] = topic_filters[filter_index].len;
            memcpy(
                sub_topic_filters[i],
                topic_filters[filter_index].data,
                topic_filters[filter_index].len
            );
            handles[i] = handle;
            filter_index += 1;
            if (filter_index == count) {
                return GGL_ERR_OK;
            }
        }
    }
    GGL_LOGE("Configured maximum subscriptions exceeded.");

    for (size_t i = 0; i < IOTCORED_MAX_SUBSCRIPTIONS; i++) {
        if (handles[i] == handle) {
            topic_filter_len[i] = 0;
        }
    }

    return GGL_ERR_NOMEM;
}

void iotcored_unregister_subscriptions(uint32_t handle) {
    GGL_MTX_SCOPE_GUARD(&mtx);

    for (size_t i = 0; i < IOTCORED_MAX_SUBSCRIPTIONS; i++) {
        if (handles[i] == handle) {
            topic_filter_len[i] = 0;
        }
    }
}

void iotcored_mqtt_receive(const IotcoredMsg *msg) {
    GGL_MTX_SCOPE_GUARD(&mtx);

    for (size_t i = 0; i < IOTCORED_MAX_SUBSCRIPTIONS; i++) {
        if ((topic_filter_len[i] != 0)
            && iotcored_mqtt_topic_filter_match(
                topic_filter_buf(i), msg->topic
            )) {
            ggl_respond(
                handles[i],
                GGL_OBJ_MAP(GGL_MAP(
                    { GGL_STR("topic"), GGL_OBJ_BUF(msg->topic) },
                    { GGL_STR("payload"), GGL_OBJ_BUF(msg->payload) }
                ))
            );
        }
    }
}
