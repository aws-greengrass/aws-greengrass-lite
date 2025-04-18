// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/aws_iot_call.h"
#include <sys/types.h>
#include <assert.h>
#include <errno.h>
#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/cleanup.h>
#include <ggl/core_bus/aws_iot_mqtt.h>
#include <ggl/core_bus/client.h> // IWYU pragma: keep (cleanup)
#include <ggl/error.h>
#include <ggl/json_decode.h>
#include <ggl/json_encode.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <pthread.h>
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
    bool ready;
    GglBuffer *client_token;
    GglAlloc *alloc;
    GglObject *result;
    GglError ret;
} CallbackCtx;

static void cleanup_pthread_cond(pthread_cond_t **cond) {
    pthread_cond_destroy(*cond);
}

static GglError get_client_token(GglObject payload, GglBuffer **client_token) {
    assert(client_token != NULL);
    assert(*client_token != NULL);

    if (ggl_obj_type(payload) != GGL_TYPE_MAP) {
        *client_token = NULL;
        return GGL_ERR_OK;
    }
    GglMap payload_map = ggl_obj_into_map(payload);

    GglObject *found;
    if (!ggl_map_get(payload_map, GGL_STR("clientToken"), &found)) {
        *client_token = NULL;
        return GGL_ERR_OK;
    }
    if (ggl_obj_type(*found) != GGL_TYPE_BUF) {
        GGL_LOGE("Invalid clientToken type.");
        return GGL_ERR_INVALID;
    }
    **client_token = ggl_obj_into_buf(*found);
    return GGL_ERR_OK;
}

static bool match_client_token(GglObject payload, GglBuffer *client_token) {
    GglBuffer *payload_client_token = &(GglBuffer) { 0 };

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

    GglBuffer topic;
    GglBuffer payload;
    GglError ret
        = ggl_aws_iot_mqtt_subscribe_parse_resp(data, &topic, &payload);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    bool decoded = true;
    ret = ggl_json_decode_destructive(
        payload, call_ctx->alloc, call_ctx->result
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to decode response payload.");
        *(call_ctx->result) = GGL_OBJ_NULL;
        decoded = false;
    }

    if (!match_client_token(*call_ctx->result, call_ctx->client_token)) {
        // Skip this message
        return GGL_ERR_OK;
    }

    if (ggl_buffer_has_suffix(topic, GGL_STR("/accepted"))) {
        if (!decoded) {
            return GGL_ERR_INVALID;
        }
        call_ctx->ret = GGL_ERR_OK;
    } else if (ggl_buffer_has_suffix(topic, GGL_STR("/rejected"))) {
        GGL_LOGE(
            "Received rejected response: %.*s", (int) payload.len, payload.data
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

    GGL_MTX_SCOPE_GUARD(call_ctx->mtx);
    call_ctx->ready = true;
    pthread_cond_signal(call_ctx->cond);
}

GglError ggl_aws_iot_call(
    GglBuffer topic, GglObject payload, GglAlloc *alloc, GglObject *result
) {
    static pthread_mutex_t mem_mtx = PTHREAD_MUTEX_INITIALIZER;
    GGL_MTX_SCOPE_GUARD(&mem_mtx);

    // TODO: Share memory for topic filter and encode
    static uint8_t topic_filter_mem[AWS_IOT_MAX_TOPIC_SIZE];
    static uint8_t json_encode_mem[GGL_MAX_IOT_CORE_API_PAYLOAD_LEN];

    GglByteVec topic_filter = GGL_BYTE_VEC(topic_filter_mem);

    GglError ret = ggl_byte_vec_append(&topic_filter, topic);
    ggl_byte_vec_chain_append(&ret, &topic_filter, GGL_STR("/+"));
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to construct response topic filter.");
        return ret;
    }

    GglBuffer payload_buf = GGL_BUF(json_encode_mem);
    ret = ggl_json_encode(payload, &payload_buf);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to encode JSON payload.");
        return ret;
    }

    pthread_condattr_t notify_condattr;
    pthread_condattr_init(&notify_condattr);
    pthread_condattr_setclock(&notify_condattr, CLOCK_MONOTONIC);
    pthread_cond_t notify_cond;
    pthread_cond_init(&notify_cond, &notify_condattr);
    pthread_condattr_destroy(&notify_condattr);
    GGL_CLEANUP(cleanup_pthread_cond, &notify_cond);
    pthread_mutex_t notify_mtx = PTHREAD_MUTEX_INITIALIZER;

    CallbackCtx ctx = {
        .mtx = &notify_mtx,
        .cond = &notify_cond,
        .ready = false,
        .client_token = &(GglBuffer) { 0 },
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
        GGL_BUF_LIST(topic_filter.buf),
        1,
        subscription_callback,
        subscription_close_callback,
        &ctx,
        &sub_handle
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Response topic subscription failed.");
        return ret;
    }

    ret = ggl_aws_iot_mqtt_publish(topic, payload_buf, 1, true);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Response topic subscription failed.");
        ggl_client_sub_close(sub_handle);
        return ret;
    }

    struct timespec timeout;
    clock_gettime(CLOCK_MONOTONIC, &timeout);
    timeout.tv_sec += IOT_RESPONSE_TIMEOUT_S;

    bool timed_out = false;

    {
        // Must be unlocked before closing subscription
        // (else subscription response may be blocked, and close would deadlock)
        GGL_MTX_SCOPE_GUARD(&notify_mtx);

        while (!ctx.ready) {
            int cond_ret
                = pthread_cond_timedwait(&notify_cond, &notify_mtx, &timeout);
            if ((cond_ret != 0) && (cond_ret != EINTR)) {
                assert(cond_ret == ETIMEDOUT);
                GGL_LOGW("Timed out waiting for a response.");
                timed_out = true;
                break;
            }
        }
    }

    if (timed_out) {
        ggl_client_sub_close(sub_handle);
    }

    return ctx.ret;
}
