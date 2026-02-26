// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <errno.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/json_decode.h>
#include <gg/json_encode.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <gg/vector.h>
#include <ggl/aws_iot_call.h>
#include <ggl/core_bus/aws_iot_mqtt.h>
#include <ggl/core_bus/client.h> // IWYU pragma: keep (cleanup)
#include <pthread.h>
#include <sys/types.h>
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
    GgBuffer *client_token;
    GgArena *alloc;
    GgObject *result;
    GgError ret;
} CallbackCtx;

static void cleanup_pthread_cond(pthread_cond_t **cond) {
    pthread_cond_destroy(*cond);
}

static GgError get_client_token(GgObject payload, GgBuffer **client_token) {
    assert(client_token != NULL);
    assert(*client_token != NULL);

    if (gg_obj_type(payload) != GG_TYPE_MAP) {
        *client_token = NULL;
        return GG_ERR_OK;
    }
    GgMap payload_map = gg_obj_into_map(payload);

    GgObject *found;
    if (!gg_map_get(payload_map, GG_STR("clientToken"), &found)) {
        *client_token = NULL;
        return GG_ERR_OK;
    }
    if (gg_obj_type(*found) != GG_TYPE_BUF) {
        GG_LOGE("Invalid clientToken type.");
        return GG_ERR_INVALID;
    }
    **client_token = gg_obj_into_buf(*found);
    return GG_ERR_OK;
}

static bool match_client_token(GgObject payload, GgBuffer *client_token) {
    GgBuffer *payload_client_token = &(GgBuffer) { 0 };

    GgError ret = get_client_token(payload, &payload_client_token);
    if (ret != GG_ERR_OK) {
        return false;
    }

    if ((client_token == NULL) && (payload_client_token == NULL)) {
        return true;
    }

    if ((client_token == NULL) || (payload_client_token == NULL)) {
        return false;
    }

    return gg_buffer_eq(*client_token, *payload_client_token);
}

static GgError subscription_callback(
    void *ctx, uint32_t handle, GgObject data
) {
    (void) handle;
    CallbackCtx *call_ctx = ctx;

    GgBuffer topic;
    GgBuffer payload = { 0 };
    GgError ret = ggl_aws_iot_mqtt_subscribe_parse_resp(data, &topic, &payload);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    bool decoded = true;
    ret = gg_json_decode_destructive(
        payload, call_ctx->alloc, call_ctx->result
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to decode response payload.");
        *(call_ctx->result) = GG_OBJ_NULL;
        decoded = false;
    }

    if (!match_client_token(*call_ctx->result, call_ctx->client_token)) {
        // Skip this message
        return GG_ERR_OK;
    }

    if (gg_buffer_has_suffix(topic, GG_STR("/accepted"))) {
        if (!decoded) {
            return GG_ERR_INVALID;
        }
        call_ctx->ret = GG_ERR_OK;
    } else if (gg_buffer_has_suffix(topic, GG_STR("/rejected"))) {
        GG_LOGE(
            "Received rejected response: %.*s", (int) payload.len, payload.data
        );
        call_ctx->ret = GG_ERR_REMOTE;
    } else {
        return GG_ERR_INVALID;
    }

    // Err to close subscription
    return GG_ERR_EXPECTED;
}

static void subscription_close_callback(void *ctx, uint32_t handle) {
    (void) handle;
    CallbackCtx *call_ctx = ctx;

    GG_MTX_SCOPE_GUARD(call_ctx->mtx);
    call_ctx->ready = true;
    pthread_cond_signal(call_ctx->cond);
}

GgError ggl_aws_iot_call(
    GgBuffer socket_name,
    GgBuffer topic,
    GgObject payload,
    bool virtual,
    GgArena *alloc,
    GgObject *result
) {
    static pthread_mutex_t mem_mtx = PTHREAD_MUTEX_INITIALIZER;
    GG_MTX_SCOPE_GUARD(&mem_mtx);

    // TODO: Share memory for topic filter and encode
    static uint8_t topic_filter_mem[AWS_IOT_MAX_TOPIC_SIZE];
    static uint8_t json_encode_mem[GGL_MAX_IOT_CORE_API_PAYLOAD_LEN];

    GgByteVec topic_filter = GG_BYTE_VEC(topic_filter_mem);

    GgError ret = gg_byte_vec_append(&topic_filter, topic);
    gg_byte_vec_chain_append(&ret, &topic_filter, GG_STR("/+"));
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to construct response topic filter.");
        return ret;
    }

    GgByteVec payload_vec = GG_BYTE_VEC(json_encode_mem);
    ret = gg_json_encode(payload, gg_byte_vec_writer(&payload_vec));
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to encode JSON payload.");
        return ret;
    }

    pthread_condattr_t notify_condattr;
    pthread_condattr_init(&notify_condattr);
    pthread_condattr_setclock(&notify_condattr, CLOCK_MONOTONIC);
    pthread_cond_t notify_cond;
    pthread_cond_init(&notify_cond, &notify_condattr);
    pthread_condattr_destroy(&notify_condattr);
    GG_CLEANUP(cleanup_pthread_cond, &notify_cond);
    pthread_mutex_t notify_mtx = PTHREAD_MUTEX_INITIALIZER;

    CallbackCtx ctx = {
        .mtx = &notify_mtx,
        .cond = &notify_cond,
        .ready = false,
        .client_token = &(GgBuffer) { 0 },
        .alloc = alloc,
        .result = result,
        .ret = GG_ERR_FAILURE,
    };

    ret = get_client_token(payload, &ctx.client_token);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    uint32_t sub_handle = 0;
    ret = ggl_aws_iot_mqtt_subscribe(
        socket_name,
        GG_BUF_LIST(topic_filter.buf),
        1,
        virtual,
        subscription_callback,
        subscription_close_callback,
        &ctx,
        &sub_handle
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Response topic subscription failed.");
        return ret;
    }

    ret = ggl_aws_iot_mqtt_publish(
        socket_name, topic, payload_vec.buf, 1, true
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Response topic subscription failed.");
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
        GG_MTX_SCOPE_GUARD(&notify_mtx);

        while (!ctx.ready) {
            int cond_ret
                = pthread_cond_timedwait(&notify_cond, &notify_mtx, &timeout);
            if ((cond_ret != 0) && (cond_ret != EINTR)) {
                assert(cond_ret == ETIMEDOUT);
                GG_LOGW("Timed out waiting for a response.");
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
