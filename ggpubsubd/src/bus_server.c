// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggpubsubd.h"
#include <sys/types.h>
#include <assert.h>
#include <core_mqtt.h>
#include <core_mqtt_config.h>
#include <ggl/core_bus/server.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

// Matches AWS IoT topic length
#define GGL_PUBSUB_MAX_TOPIC_LENGTH 256

/// Maximum number of local subscriptions.
/// Can be configured with `-DGGL_PUBSUB_MAX_SUBSCRIPTIONS=<N>`.
#ifndef GGL_PUBSUB_MAX_SUBSCRIPTIONS
#define GGL_PUBSUB_MAX_SUBSCRIPTIONS (GGL_COREBUS_MAX_CLIENTS - 1)
#endif

static_assert(
    GGL_PUBSUB_MAX_SUBSCRIPTIONS < GGL_COREBUS_MAX_CLIENTS,
    "GGL_PUBSUB_MAX_SUBSCRIPTIONS too large; if it is >= core bus client "
    "maximum, then subscriptions can block publishes from being handled."
);

static uint32_t sub_handle[GGL_PUBSUB_MAX_SUBSCRIPTIONS];
static uint32_t sub_topic_filter[GGL_PUBSUB_MAX_SUBSCRIPTIONS]
                                [GGL_PUBSUB_MAX_TOPIC_LENGTH];
// Stores length -1 so fits in uint8_t
static uint8_t sub_topic_length[GGL_PUBSUB_MAX_SUBSCRIPTIONS];

static_assert(
    GGL_PUBSUB_MAX_TOPIC_LENGTH - 1 <= UINT8_MAX,
    "GGL_PUBSUB_MAX_TOPIC_LENGTH does not fit in an uint8_t."
);

static void rpc_publish(void *ctx, GglMap params, uint32_t handle);
static void rpc_subscribe(void *ctx, GglMap params, uint32_t handle);

// coreMQTT mtx APIs need defining since we link to it, but we only use topic
// matching so they should never be called.

pthread_mutex_t *coremqtt_get_send_mtx(const MQTTContext_t *ctx) {
    (void) ctx;
    assert(false);
    return NULL;
}

pthread_mutex_t *coremqtt_get_state_mtx(const MQTTContext_t *ctx) {
    (void) ctx;
    assert(false);
    return NULL;
}

GglError run_ggpubsubd(void) {
    GglRpcMethodDesc handlers[] = {
        { GGL_STR("publish"), false, rpc_publish, NULL },
        { GGL_STR("subscribe"), true, rpc_subscribe, NULL },
    };
    size_t handlers_len = sizeof(handlers) / sizeof(handlers[0]);

    GglError ret = ggl_listen(GGL_STR("pubsub"), handlers, handlers_len);

    GGL_LOGE("ggpubsubd", "Exiting with error %u.", (unsigned) ret);
    return ret;
}

static void rpc_publish(void *ctx, GglMap params, uint32_t handle) {
    (void) ctx;
    GGL_LOGD("publish", "Handling request from %u.", handle);

    GglBuffer topic;
    GglObject *val;
    bool found = ggl_map_get(params, GGL_STR("topic"), &val);
    if (!found) {
        GGL_LOGE("publish", "Params missing topic.");
        ggl_return_err(handle, GGL_ERR_INVALID);
        return;
    }
    if (val->type != GGL_TYPE_BUF) {
        GGL_LOGE("publish", "topic is not a string.");
        ggl_return_err(handle, GGL_ERR_INVALID);
        return;
    }

    topic = val->buf;

    if (topic.len > GGL_PUBSUB_MAX_TOPIC_LENGTH) {
        GGL_LOGE("publish", "Topic too large.");
        ggl_return_err(handle, GGL_ERR_RANGE);
        return;
    }

    for (size_t i = 0; i < GGL_PUBSUB_MAX_SUBSCRIPTIONS; i++) {
        if (sub_handle[i] != 0) {
            bool matches = false;
            MQTT_MatchTopic(
                (char *) topic.data,
                (uint16_t) topic.len,
                (char *) sub_topic_filter[i],
                (uint16_t) sub_topic_length[i] + 1,
                &matches
            );
            if (matches) {
                ggl_respond(sub_handle[i], GGL_OBJ(params));
            }
        }
    }

    ggl_respond(handle, GGL_OBJ_NULL());
}

static GglError register_subscription(
    GglBuffer topic_filter, uint32_t handle, uint32_t **handle_ptr
) {
    for (size_t i = 0; i < GGL_PUBSUB_MAX_SUBSCRIPTIONS; i++) {
        if (sub_handle[i] == 0) {
            sub_handle[i] = handle;
            memcpy(sub_topic_filter[i], topic_filter.data, topic_filter.len);
            sub_topic_length[i] = (uint8_t) (topic_filter.len - 1);
            *handle_ptr = &sub_handle[i];
            return GGL_ERR_OK;
        }
    }
    GGL_LOGE("subscribe", "Configured maximum subscriptions exceeded.");
    return GGL_ERR_NOMEM;
}

static void release_subscription(void *ctx, uint32_t handle) {
    (void) handle;
    uint32_t *handle_ptr = ctx;
    size_t index = (size_t) (handle_ptr - sub_handle);
    assert(sub_handle[index] == handle);
    sub_handle[index] = 0;
}

static void rpc_subscribe(void *ctx, GglMap params, uint32_t handle) {
    (void) ctx;
    GGL_LOGD("subscribe", "Handling request from %u.", handle);

    GglBuffer topic_filter = { 0 };

    GglObject *val;
    if (ggl_map_get(params, GGL_STR("topic_filter"), &val)
        && (val->type == GGL_TYPE_BUF)) {
        topic_filter = val->buf;
        if (topic_filter.len > GGL_PUBSUB_MAX_TOPIC_LENGTH) {
            GGL_LOGE("subscribe", "Topic filter too large.");
            ggl_return_err(handle, GGL_ERR_RANGE);
            return;
        }
        if (topic_filter.len == 0) {
            GGL_LOGE("subscribe", "Topic filter can't be zero length.");
            ggl_return_err(handle, GGL_ERR_RANGE);
            return;
        }
    } else {
        GGL_LOGE("subscribe", "Received invalid arguments.");
        ggl_return_err(handle, GGL_ERR_INVALID);
        return;
    }

    uint32_t *handle_ptr;
    GglError ret = register_subscription(topic_filter, handle, &handle_ptr);
    if (ret != GGL_ERR_OK) {
        ggl_return_err(handle, ret);
        return;
    }

    ggl_sub_accept(handle, release_subscription, handle_ptr);
}
