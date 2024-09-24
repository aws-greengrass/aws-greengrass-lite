// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/aws_iot_call.h"
#include <sys/types.h>
#include <errno.h>
#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/aws_iot_mqtt.h>
#include <ggl/core_bus/client.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/json_decode.h>
#include <ggl/json_encode.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <pthread.h>
#include <sys/time.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>

#define AWS_IOT_MAX_TOPIC_SIZE 256

#define IOT_RESPONSE_TIMEOUT_S 30

#ifndef GGL_MAX_IOT_CORE_API_PAYLOAD_LEN
#define GGL_MAX_IOT_CORE_API_PAYLOAD_LEN 5000
#endif

typedef struct {
    pthread_mutex_t *mtx;
    pthread_cond_t *cond;
    GglBuffer *client_token;
    GglAlloc *alloc;
    GglObject *result;
    GglError ret;
} CallbackCtx;

static GglError get_client_token(GglObject payload, GglBuffer **client_token) {
    *client_token = NULL;
    if (payload.type != GGL_TYPE_MAP) {
        return GGL_ERR_OK;
    }
    GglObject *found;
    if (!ggl_map_get(payload.map, GGL_STR("clientToken"), &found)) {
        return GGL_ERR_OK;
    }
    if (found->type != GGL_TYPE_BUF) {
        GGL_LOGE("iot_core_call", "Invalid clientToken type.");
        return GGL_ERR_INVALID;
    }
    *client_token = &found->buf;
    return GGL_ERR_OK;
}

static bool match_client_token(GglObject payload, GglBuffer *client_token) {
    GglBuffer *payload_client_token = NULL;

    GglError ret = get_client_token(payload, &payload_client_token);
    if (ret != GGL_ERR_OK) {
        return false;
    }

    if ((client_token == NULL) && (payload_client_token == NULL)) {
        return true;
    }

    if ((client_token == NULL) || (payload_client_token == NULL)) {
        return false;
    }

    return ggl_buffer_eq(*client_token, *payload_client_token);
}

static GglError subscription_callback(
    void *ctx, uint32_t handle, GglObject data
) {
    (void) handle;
    CallbackCtx *call_ctx = ctx;

    GglBuffer *topic;
    GglBuffer *payload;
    GglError ret
        = ggl_aws_iot_mqtt_subscribe_parse_resp(data, &topic, &payload);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    bool decoded = true;
    ret = ggl_json_decode_destructive(
        *payload, call_ctx->alloc, call_ctx->result
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("iot_core_call", "Failed to decode response payload.");
        *(call_ctx->result) = GGL_OBJ_NULL();
        decoded = false;
    }

    if (!match_client_token(*call_ctx->result, call_ctx->client_token)) {
        // Skip this message
        return GGL_ERR_OK;
    }

    if (ggl_buffer_has_suffix(*topic, GGL_STR("/accepted"))) {
        if (!decoded) {
            return GGL_ERR_INVALID;
        }
        call_ctx->ret = GGL_ERR_OK;
    } else if (ggl_buffer_has_suffix(*topic, GGL_STR("/rejected"))) {
        GGL_LOGE(
            "iot_core_call",
            "Received rejected response: %.*s",
            (int) payload->len,
            payload->data
        );
        call_ctx->ret = GGL_ERR_REMOTE;
    } else {
        return GGL_ERR_INVALID;
    }

    // Err to close subscription
    return GGL_ERR_EXPECTED;
}

static void subscription_close_callback(void *ctx, uint32_t handle) {
    (void) handle;
    CallbackCtx *call_ctx = ctx;

    pthread_mutex_lock(call_ctx->mtx);
    pthread_cond_signal(call_ctx->cond);
    pthread_mutex_unlock(call_ctx->mtx);
}

GglError ggl_aws_iot_call(
    GglBuffer topic, GglObject payload, GglAlloc *alloc, GglObject *result
) {
    static pthread_mutex_t mem_mtx = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&mem_mtx);
    GGL_DEFER(pthread_mutex_unlock, mem_mtx);

    // TODO: Share memory for topics and encode
    static uint8_t accepted_topic_mem[AWS_IOT_MAX_TOPIC_SIZE];
    static uint8_t rejected_topic_mem[AWS_IOT_MAX_TOPIC_SIZE];
    static uint8_t json_encode_mem[GGL_MAX_IOT_CORE_API_PAYLOAD_LEN];

    GglByteVec accepted_topic = GGL_BYTE_VEC(accepted_topic_mem);
    GglByteVec rejected_topic = GGL_BYTE_VEC(rejected_topic_mem);

    GglError ret = ggl_byte_vec_append(&accepted_topic, topic);
    ggl_byte_vec_chain_append(&ret, &accepted_topic, GGL_STR("/accepted"));
    ggl_byte_vec_chain_append(&ret, &rejected_topic, topic);
    ggl_byte_vec_chain_append(&ret, &rejected_topic, GGL_STR("/rejected"));
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("iot_core_call", "Failed to construct response topics.");
        return ret;
    }

    pthread_mutex_t notify_mtx = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t notify_cond = PTHREAD_COND_INITIALIZER;
    CallbackCtx ctx = {
        .mtx = &notify_mtx,
        .cond = &notify_cond,
        .client_token = NULL,
        .alloc = alloc,
        .result = result,
        .ret = GGL_ERR_FAILURE,
    };

    ret = get_client_token(payload, &ctx.client_token);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    uint32_t sub_handle = 0;
    ret = ggl_aws_iot_mqtt_subscribe(
        GGL_BUF_LIST(accepted_topic.buf, rejected_topic.buf),
        1,
        subscription_callback,
        subscription_close_callback,
        &ctx,
        &sub_handle
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("iot_core_call", "Response topic subscription failed.");
        return ret;
    }

    GglBuffer payload_buf = GGL_BUF(json_encode_mem);
    ret = ggl_json_encode(payload, &payload_buf);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("iot_core_call", "Failed to encode JSON payload.");
        ggl_client_sub_close(sub_handle);
        return ret;
    }

    // Must be unlocked before closing subscription
    // (else subscription response may be blocked, and close would deadlock)
    pthread_mutex_lock(&notify_mtx);

    ret = ggl_aws_iot_mqtt_publish(topic, payload_buf, 1, true);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("iot_core_call", "Response topic subscription failed.");
        pthread_mutex_unlock(&notify_mtx);
        ggl_client_sub_close(sub_handle);
        return ret;
    }

    struct timeval now;
    gettimeofday(&now, NULL);
    struct timespec timeout = {
        .tv_sec = now.tv_sec + IOT_RESPONSE_TIMEOUT_S,
        .tv_nsec = now.tv_usec * 1000,
    };

    int cont_ret;
    do {
        cont_ret = pthread_cond_timedwait(&notify_cond, &notify_mtx, &timeout);
    } while (cont_ret == EINTR);

    pthread_mutex_unlock(&notify_mtx);
    ggl_client_sub_close(sub_handle);

    return ctx.ret;
}
