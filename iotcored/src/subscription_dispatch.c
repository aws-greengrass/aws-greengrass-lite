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
#include <stdbool.h>
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
static uint8_t topic_qos[IOTCORED_MAX_SUBSCRIPTIONS];
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

static uint32_t mqtt_status_handles[IOTCORED_MAX_SUBSCRIPTIONS];
static pthread_mutex_t mqtt_status_mtx = PTHREAD_MUTEX_INITIALIZER;

static GglBuffer topic_filter_buf(size_t index) {
    return ggl_buffer_substr(
        GGL_BUF(sub_topic_filters[index]), 0, topic_filter_len[index]
    );
}

GglError iotcored_register_subscriptions(
    GglBuffer *topic_filters, size_t count, uint32_t handle, uint8_t qos
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

    GGL_LOGD("Registering subscriptions.");

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
            topic_qos[i] = qos;
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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void iotcored_unregister_subscriptions(uint32_t handle, bool unsubscribe) {
    GGL_MTX_SCOPE_GUARD(&mtx);

    for (size_t i = 0; i < IOTCORED_MAX_SUBSCRIPTIONS; i++) {
        if (handles[i] == handle) {
            if (unsubscribe) {
                size_t j;
                for (j = 0; j < IOTCORED_MAX_SUBSCRIPTIONS; j++) {
                    if (i == j) {
                        continue;
                    }
                    if ((topic_filter_len[j] != 0)
                        && (topic_filter_len[i] == topic_filter_len[j])
                        && (memcmp(
                                sub_topic_filters[i],
                                sub_topic_filters[j],
                                topic_filter_len[i]
                            )
                            == 0)) {
                        // Found a matching topic filter. No need to check
                        // further.
                        break;
                    }
                }

                // This is the only subscription to this topic. Send an
                // unsubscribe.
                if (j == IOTCORED_MAX_SUBSCRIPTIONS) {
                    GglBuffer buf[] = { topic_filter_buf(i) };
                    iotcored_mqtt_unsubscribe(buf, 1U);
                }
            }

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
            ggl_sub_respond(
                handles[i],
                GGL_OBJ_MAP(GGL_MAP(
                    { GGL_STR("topic"), GGL_OBJ_BUF(msg->topic) },
                    { GGL_STR("payload"), GGL_OBJ_BUF(msg->payload) }
                ))
            );
        }
    }
}

GglError iotcored_mqtt_status_update_register(uint32_t handle) {
    GGL_MTX_SCOPE_GUARD(&mqtt_status_mtx);
    for (size_t i = 0; i < IOTCORED_MAX_SUBSCRIPTIONS; i++) {
        if (mqtt_status_handles[i] == 0) {
            mqtt_status_handles[i] = handle;
            return GGL_ERR_OK;
        }
    }
    return GGL_ERR_NOMEM;
}

void iotcored_mqtt_status_update_unregister(uint32_t handle) {
    GGL_MTX_SCOPE_GUARD(&mqtt_status_mtx);
    for (size_t i = 0; i < IOTCORED_MAX_SUBSCRIPTIONS; i++) {
        if (mqtt_status_handles[i] == handle) {
            mqtt_status_handles[i] = 0;
            return;
        }
    }
}

void iotcored_mqtt_status_update_send(GglObject status) {
    GGL_MTX_SCOPE_GUARD(&mqtt_status_mtx);

    for (size_t i = 0; i < IOTCORED_MAX_SUBSCRIPTIONS; i++) {
        if (mqtt_status_handles[i] != 0) {
            ggl_sub_respond(mqtt_status_handles[i], status);
        }
    }
}

void iotcored_re_register_all_subs(void) {
    GGL_MTX_SCOPE_GUARD(&mtx);

    for (size_t i = 0; i < IOTCORED_MAX_SUBSCRIPTIONS; i++) {
        if (topic_filter_len[i] != 0) {
            GglBuffer buffer
                = { .data = sub_topic_filters[i], .len = topic_filter_len[i] };
            GGL_LOGD(
                "Subscribing again to:  %.*s",
                (int) topic_filter_len[i],
                sub_topic_filters[i]
            );
            if (iotcored_mqtt_subscribe(&buffer, 1, topic_qos[i])
                != GGL_ERR_OK) {
                topic_filter_len[i] = 0;
                GGL_LOGE("Failed to subscribe to topic filter.");
            }
        }
    }
}
