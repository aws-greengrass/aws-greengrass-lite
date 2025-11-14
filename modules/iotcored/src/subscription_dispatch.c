// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "subscription_dispatch.h"
#include "mqtt.h"
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <ggl/core_bus/server.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
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

static GgBuffer topic_filter_buf(size_t index) {
    return gg_buffer_substr(
        GG_BUF(sub_topic_filters[index]), 0, topic_filter_len[index]
    );
}

GgError iotcored_register_subscriptions(
    GgBuffer *topic_filters, size_t count, uint32_t handle, uint8_t qos
) {
    for (size_t i = 0; i < count; i++) {
        if (topic_filters[i].len == 0) {
            GG_LOGE("Attempted to register a 0 length topic filter.");
            return GG_ERR_INVALID;
        }
    }
    for (size_t i = 0; i < count; i++) {
        if (topic_filters[i].len > AWS_IOT_MAX_TOPIC_SIZE) {
            GG_LOGE("Topic filter exceeds max length.");
            return GG_ERR_RANGE;
        }
    }

    GG_LOGD("Registering subscriptions.");

    GG_MTX_SCOPE_GUARD(&mtx);

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
                return GG_ERR_OK;
            }
        }
    }
    GG_LOGE("Configured maximum subscriptions exceeded.");

    for (size_t i = 0; i < IOTCORED_MAX_SUBSCRIPTIONS; i++) {
        if (handles[i] == handle) {
            topic_filter_len[i] = 0;
        }
    }

    return GG_ERR_NOMEM;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void iotcored_unregister_subscriptions(uint32_t handle, bool unsubscribe) {
    GG_MTX_SCOPE_GUARD(&mtx);

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
                    GgBuffer buf[] = { topic_filter_buf(i) };
                    // TODO: Should these be retried? If offline, should be
                    // queued up until online?
                    (void) iotcored_mqtt_unsubscribe(buf, 1U);
                }
            }

            topic_filter_len[i] = 0;
        }
    }
}

void iotcored_mqtt_receive(const IotcoredMsg *msg) {
    GG_MTX_SCOPE_GUARD(&mtx);

    for (size_t i = 0; i < IOTCORED_MAX_SUBSCRIPTIONS; i++) {
        if ((topic_filter_len[i] != 0)
            && iotcored_mqtt_topic_filter_match(
                topic_filter_buf(i), msg->topic
            )) {
            ggl_sub_respond(
                handles[i],
                gg_obj_map(GG_MAP(
                    gg_kv(GG_STR("topic"), gg_obj_buf(msg->topic)),
                    gg_kv(GG_STR("payload"), gg_obj_buf(msg->payload))
                ))
            );
        }
    }
}

GgError iotcored_mqtt_status_update_register(uint32_t handle) {
    GG_MTX_SCOPE_GUARD(&mqtt_status_mtx);
    for (size_t i = 0; i < IOTCORED_MAX_SUBSCRIPTIONS; i++) {
        if (mqtt_status_handles[i] == 0) {
            mqtt_status_handles[i] = handle;
            return GG_ERR_OK;
        }
    }
    return GG_ERR_NOMEM;
}

void iotcored_mqtt_status_update_unregister(uint32_t handle) {
    GG_MTX_SCOPE_GUARD(&mqtt_status_mtx);
    for (size_t i = 0; i < IOTCORED_MAX_SUBSCRIPTIONS; i++) {
        if (mqtt_status_handles[i] == handle) {
            mqtt_status_handles[i] = 0;
            return;
        }
    }
}

void iotcored_mqtt_status_update_send(GgObject status) {
    GG_MTX_SCOPE_GUARD(&mqtt_status_mtx);

    for (size_t i = 0; i < IOTCORED_MAX_SUBSCRIPTIONS; i++) {
        if (mqtt_status_handles[i] != 0) {
            ggl_sub_respond(mqtt_status_handles[i], status);
        }
    }
}

void iotcored_re_register_all_subs(void) {
    GG_MTX_SCOPE_GUARD(&mtx);

    for (size_t i = 0; i < IOTCORED_MAX_SUBSCRIPTIONS; i++) {
        if (topic_filter_len[i] != 0) {
            GgBuffer buffer
                = { .data = sub_topic_filters[i], .len = topic_filter_len[i] };
            GG_LOGD(
                "Subscribing again to:  %.*s",
                (int) topic_filter_len[i],
                sub_topic_filters[i]
            );
            if (iotcored_mqtt_subscribe(&buffer, 1, topic_qos[i])
                != GG_ERR_OK) {
                topic_filter_len[i] = 0;
                GG_LOGE("Failed to subscribe to topic filter.");
            }
        }
    }
}
