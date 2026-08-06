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
/// True if the slot is a virtual registration: routed to like any other slot,
/// but with no cloud subscription behind it (never subscribed, unsubscribed,
/// or re-subscribed with AWS IoT Core).
static bool sub_virtual[IOTCORED_MAX_SUBSCRIPTIONS];
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

static uint32_t mqtt_status_handles[IOTCORED_MAX_SUBSCRIPTIONS];
static pthread_mutex_t mqtt_status_mtx = PTHREAD_MUTEX_INITIALIZER;

static GgBuffer topic_filter_buf(size_t index) {
    return gg_buffer_substr(
        GG_BUF(sub_topic_filters[index]), 0, topic_filter_len[index]
    );
}

GgError iotcored_register_subscriptions(
    GgBuffer *topic_filters,
    size_t count,
    uint32_t handle,
    uint8_t qos,
    bool is_virtual
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
            sub_virtual[i] = is_virtual;
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
            sub_virtual[i] = false;
        }
    }

    return GG_ERR_NOMEM;
}

/// True if another slot holds a cloud subscription to the same topic filter.
/// Virtual slots don't count, as they have no cloud subscription.
static bool cloud_sub_shares_filter(size_t index) {
    for (size_t i = 0; i < IOTCORED_MAX_SUBSCRIPTIONS; i++) {
        if ((i == index) || (topic_filter_len[i] == 0) || sub_virtual[i]) {
            continue;
        }
        if ((topic_filter_len[i] == topic_filter_len[index])
            && (memcmp(
                    sub_topic_filters[index],
                    sub_topic_filters[i],
                    topic_filter_len[index]
                )
                == 0)) {
            return true;
        }
    }
    return false;
}

void iotcored_unregister_subscriptions(uint32_t handle, bool unsubscribe) {
    GG_MTX_SCOPE_GUARD(&mtx);

    for (size_t i = 0; i < IOTCORED_MAX_SUBSCRIPTIONS; i++) {
        // A freed slot keeps its stale handle, so only act on live slots.
        if ((topic_filter_len[i] == 0) || (handles[i] != handle)) {
            continue;
        }

        // Send an MQTT unsubscribe only if this slot was the last cloud
        // subscription on the filter. Virtual slots never subscribed, so
        // there is nothing to unsubscribe.
        if (unsubscribe && !sub_virtual[i] && !cloud_sub_shares_filter(i)) {
            GgBuffer buf[] = { topic_filter_buf(i) };
            // TODO: Should these be retried? If offline, should be
            // queued up until online?
            (void) iotcored_mqtt_unsubscribe(buf, 1U);
        }

        topic_filter_len[i] = 0;
        sub_virtual[i] = false;
    }
}

void iotcored_mqtt_receive(const IotcoredMsg *msg) {
    GG_MTX_SCOPE_GUARD(&mtx);

    bool matched = false;

    // Matching is by topic filter alone; virtual slots receive the same way
    // as cloud ones. This is how a virtual registration receives AWS IoT Core
    // direct messages, which arrive without any cloud subscription.
    for (size_t i = 0; i < IOTCORED_MAX_SUBSCRIPTIONS; i++) {
        if ((topic_filter_len[i] != 0)
            && iotcored_mqtt_topic_filter_match(
                topic_filter_buf(i), msg->topic
            )) {
            matched = true;
            ggl_sub_respond(
                handles[i],
                gg_obj_map(GG_MAP(
                    gg_kv(GG_STR("topic"), gg_obj_buf(msg->topic)),
                    gg_kv(GG_STR("payload"), gg_obj_buf(msg->payload))
                ))
            );
        }
    }

    if (!matched) {
        GG_LOGW(
            "Dropping message on topic %.*s: no matching subscription.",
            (int) msg->topic.len,
            msg->topic.data
        );
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
        if (topic_filter_len[i] == 0) {
            continue;
        }

        // A virtual slot has no cloud subscription to restore; it keeps
        // receiving without any action. Subscribing it here would silently
        // turn it into a cloud subscription.
        if (sub_virtual[i]) {
            continue;
        }

        GgBuffer buffer
            = { .data = sub_topic_filters[i], .len = topic_filter_len[i] };
        GG_LOGD(
            "Subscribing again to:  %.*s",
            (int) topic_filter_len[i],
            sub_topic_filters[i]
        );
        if (iotcored_mqtt_subscribe(&buffer, 1, topic_qos[i]) != GG_ERR_OK) {
            topic_filter_len[i] = 0;
            GG_LOGE("Failed to subscribe to topic filter.");
        }
    }
}
