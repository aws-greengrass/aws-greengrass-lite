// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggpubsubd.h"
#include <sys/types.h>
#include <assert.h>
#include <core_mqtt.h>
#include <core_mqtt_config.h>
#include <ggl/buffer.h>
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

static GglError rpc_publish(void *ctx, GglMap params, uint32_t handle);
static GglError rpc_subscribe(void *ctx, GglMap params, uint32_t handle);

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

    GglError ret = ggl_listen(GGL_STR("gg_pubsub"), handlers, handlers_len);

    GGL_LOGE("Exiting with error %u.", (unsigned) ret);
    return ret;
}

static GglError rpc_publish(void *ctx, GglMap params, uint32_t handle) {
    (void) ctx;
    GGL_LOGD("Handling request from %u.", handle);

    GglObject *val;
    bool found = ggl_map_get(params, GGL_STR("topic"), &val);
    if (!found) {
        GGL_LOGE("Params missing topic.");
        return GGL_ERR_INVALID;
    }
    if (ggl_obj_type(*val) != GGL_TYPE_BUF) {
        GGL_LOGE("topic is not a string.");
        return GGL_ERR_INVALID;
    }

    GglBuffer topic = ggl_obj_into_buf(*val);

    if (topic.len > GGL_PUBSUB_MAX_TOPIC_LENGTH) {
        GGL_LOGE("Topic too large.");
        return GGL_ERR_RANGE;
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
                ggl_sub_respond(sub_handle[i], ggl_obj_map(params));
            }
        }
    }

    ggl_respond(handle, GGL_OBJ_NULL);
    return GGL_ERR_OK;
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
    GGL_LOGE("Configured maximum subscriptions exceeded.");
    return GGL_ERR_NOMEM;
}

static void release_subscription(void *ctx, uint32_t handle) {
    (void) handle;
    uint32_t *handle_ptr = ctx;
    size_t index = (size_t) (handle_ptr - sub_handle);
    assert(sub_handle[index] == handle);
    sub_handle[index] = 0;
}

static GglError rpc_subscribe(void *ctx, GglMap params, uint32_t handle) {
    (void) ctx;
    GGL_LOGD("Handling request from %u.", handle);

    GglBuffer topic_filter = { 0 };

    GglObject *val;
    if (ggl_map_get(params, GGL_STR("topic_filter"), &val)
        && (ggl_obj_type(*val) == GGL_TYPE_BUF)) {
        topic_filter = ggl_obj_into_buf(*val);
        if (topic_filter.len > GGL_PUBSUB_MAX_TOPIC_LENGTH) {
            GGL_LOGE("Topic filter too large.");
            return GGL_ERR_RANGE;
        }
        if (topic_filter.len == 0) {
            GGL_LOGE("Topic filter can't be zero length.");
            return GGL_ERR_RANGE;
        }
    } else {
        GGL_LOGE("Received invalid arguments.");
        return GGL_ERR_INVALID;
    }

    uint32_t *handle_ptr;
    GglError ret = register_subscription(topic_filter, handle, &handle_ptr);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ggl_sub_accept(handle, release_subscription, handle_ptr);
    return GGL_ERR_OK;
}
