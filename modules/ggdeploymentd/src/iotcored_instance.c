// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "iotcored_instance.h"
#include <assert.h>
#include <errno.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/log.h>
#include <gg/utils.h>
#include <ggl/core_bus/aws_iot_mqtt.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/process.h>
#include <limits.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// TODO: Remove retry by pre-creating the socket before spawn (socket
// activation).
#define SUBSCRIBE_RETRY_INTERVAL_MS 500
#define IOTCORED_INSTANCE_NAME "iotcoreddeploy"
#define MAX_ENDPOINT_LEN 128
#define MAX_THING_NAME_LEN 128
// MQTT client ID: thingName + suffix. Must match IoT policy pattern thingName*
// and stay within 128-byte MQTT client ID limit.
// TODO: Use a dynamic suffix if multiple iotcored instances are needed
// concurrently.
#define MAX_CLIENT_ID_LEN 128
#define CLIENT_ID_SUFFIX "#endpoint-switch"

typedef struct {
    pthread_mutex_t mtx;
    pthread_cond_t cond;
    bool connected;
} ConnectionCtx;

static GgError connection_status_callback(
    void *ctx, uint32_t handle, GgObject data
) {
    (void) handle;
    ConnectionCtx *conn_ctx = ctx;

    bool connected = false;
    GgError ret = ggl_aws_iot_mqtt_connection_status_parse(data, &connected);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    if (connected) {
        GG_MTX_SCOPE_GUARD(&conn_ctx->mtx);
        conn_ctx->connected = true;
        pthread_cond_signal(&conn_ctx->cond);
    }

    return GG_ERR_OK;
}

GgError iotcored_instance_start(
    IotcoredInstance *ctx, GgBuffer iotcored_path, GgBuffer endpoint
) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->handle = (GglProcessHandle) { -1 };

    static uint8_t thing_name_mem[MAX_THING_NAME_LEN + 1];
    GgArena alloc = gg_arena_init(
        gg_buffer_substr(GG_BUF(thing_name_mem), 0, MAX_THING_NAME_LEN)
    );
    GgBuffer thing_name = { 0 };
    GgError ret = ggl_gg_config_read_str(
        GG_BUF_LIST(GG_STR("system"), GG_STR("thingName")), &alloc, &thing_name
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to read thingName.");
        return ret;
    }
    char client_id[MAX_CLIENT_ID_LEN + 1];
    int client_id_len = snprintf(
        client_id,
        sizeof(client_id),
        "%.*s" CLIENT_ID_SUFFIX,
        (int) thing_name.len,
        thing_name.data
    );
    if (client_id_len >= (int) sizeof(client_id)) {
        GG_LOGE(
            "MQTT client ID %.*s" CLIENT_ID_SUFFIX " exceeds 128-byte limit.",
            (int) thing_name.len,
            thing_name.data
        );
        return GG_ERR_RANGE;
    }

    char endpoint_buf[MAX_ENDPOINT_LEN + 1];
    if (endpoint.len >= sizeof(endpoint_buf)) {
        GG_LOGE("Endpoint too long: %.*s.", (int) endpoint.len, endpoint.data);
        return GG_ERR_RANGE;
    }
    memcpy(endpoint_buf, endpoint.data, endpoint.len);
    endpoint_buf[endpoint.len] = '\0';

    char path_buf[PATH_MAX];
    if (iotcored_path.len >= sizeof(path_buf)) {
        GG_LOGE(
            "iotcored path too long: %.*s.",
            (int) iotcored_path.len,
            iotcored_path.data
        );
        return GG_ERR_RANGE;
    }
    memcpy(path_buf, iotcored_path.data, iotcored_path.len);
    path_buf[iotcored_path.len] = '\0';

    const char *args[] = {
        path_buf,  "-n", IOTCORED_INSTANCE_NAME, "-e", endpoint_buf, "-i",
        client_id, NULL,
    };

    ret = ggl_process_spawn(args, NULL, &ctx->handle);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to spawn iotcored instance.");
        ctx->handle = (GglProcessHandle) { -1 };
        return ret;
    }

    GG_LOGD("Spawned iotcored instance (pid=%d).", ctx->handle.val);
    return GG_ERR_OK;
}

static void cleanup_pthread_cond(pthread_cond_t **cond) {
    if (*cond != NULL) {
        pthread_cond_destroy(*cond);
    }
}

GgError iotcored_await_connection(GgBuffer socket_name, uint32_t timeout_s) {
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_t cond;
    pthread_cond_init(&cond, &attr);
    pthread_condattr_destroy(&attr);
    GG_CLEANUP(cleanup_pthread_cond, &cond);

    pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

    ConnectionCtx ctx = {
        .mtx = mtx,
        .cond = cond,
        .connected = false,
    };

    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += timeout_s;

    uint32_t sub_handle = 0;
    GG_CLEANUP(cleanup_ggl_client_sub_close, sub_handle);

    while (true) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if ((now.tv_sec > deadline.tv_sec)
            || ((now.tv_sec == deadline.tv_sec)
                && (now.tv_nsec >= deadline.tv_nsec))) {
            return GG_ERR_FAILURE;
        }

        GgError ret = ggl_aws_iot_mqtt_connection_status(
            socket_name, connection_status_callback, NULL, &ctx, &sub_handle
        );
        if (ret == GG_ERR_OK) {
            break;
        }

        (void) gg_sleep_ms(SUBSCRIBE_RETRY_INTERVAL_MS);
    }

    bool timed_out = false;
    {
        GG_MTX_SCOPE_GUARD(&ctx.mtx);

        while (!ctx.connected) {
            int cond_ret
                = pthread_cond_timedwait(&ctx.cond, &ctx.mtx, &deadline);
            if ((cond_ret != 0) && (cond_ret != EINTR)) {
                assert(cond_ret == ETIMEDOUT);
                timed_out = true;
                break;
            }
        }
    }

    if (timed_out) {
        return GG_ERR_FAILURE;
    }

    return GG_ERR_OK;
}

void iotcored_instance_stop(IotcoredInstance *ctx) {
    if (ctx->handle.val > 0) {
        GG_LOGD("Stopping iotcored instance (pid=%d).", ctx->handle.val);
        (void) ggl_process_kill(ctx->handle, 5);
        ctx->handle = (GglProcessHandle) { -1 };
    }
}
