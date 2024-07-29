// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "client_common.h"
#include "ggl/core_bus/client.h"
#include "object_serde.h"
#include "types.h"
#include <assert.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/eventstream/decode.h>
#include <ggl/eventstream/types.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/socket_epoll.h>
#include <ggl/socket_handle.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

// This must be separate C file from rest of core bus functionality as it
// creates a thread on startup using a constructor function.
// When including a .a, the linker only uses .o files that resolve a needed
// symbol. Since this is a separate .o, that means it will only be included if
// ggl_subscribe is used by the binary, and the thread is only created in
// binaries using ggl_subscribe functionality.

/// Maximum number of core-bus connections.
/// Can be configured with `-DGGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS=<N>`.
#ifndef GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS
#define GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS 50
#endif

#define PAYLOAD_MAX_SUBOBJECTS 50

static_assert(
    GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS < UINT16_MAX,
    "Max subscriptions cannot exceed UINT16_MAX."
);

typedef struct {
    GglSubscribeCallback on_response;
    GglSubscribeCloseCallback on_close;
    void *ctx;
} SubCallbacks;

static SubCallbacks sub_callbacks[GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS];

static GglError reset_sub_state(uint32_t handle, size_t index);

static int sub_fds[GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS];
static uint16_t sub_generations[GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS];

GglSocketPool pool = {
    .max_fds = GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS,
    .fds = sub_fds,
    .generations = sub_generations,
    .on_register = reset_sub_state,
};

__attribute__((constructor)) static void init_sub_pool(void) {
    ggl_socket_pool_init(&pool);
}

static void *subscription_thread(void *args);

static int epoll_fd = -1;

/// Initializes subscription epoll and starts epoll thread.
/// Runs at startup (before main).
__attribute__((constructor)) static void start_subscription_thread(void) {
    GglError ret = ggl_socket_epoll_create(&epoll_fd);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "core-bus-client",
            "Failed to create epoll for subscription responses."
        );
        // exit() is not re-entrant and this is safe as long as no spawned
        // thread can call exit()
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        exit(-1);
    }

    pthread_t sub_thread = { 0 };
    int sys_ret = pthread_create(&sub_thread, NULL, subscription_thread, NULL);
    if (sys_ret != 0) {
        GGL_LOGE(
            "core-bus-client", "Failed to create subscription response thread."
        );
        // exit() is not re-entrant and this is safe as long as no spawned
        // thread can call exit()
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        exit(-1);
    }
    pthread_detach(sub_thread);
}

static GglError reset_sub_state(uint32_t handle, size_t index) {
    (void) handle;
    sub_callbacks[index] = (SubCallbacks) { 0 };
    return GGL_ERR_OK;
}

static void set_sub_callbacks(void *ctx, size_t index) {
    SubCallbacks *callbacks = ctx;
    sub_callbacks[index] = *callbacks;
}

static void get_sub_callbacks(void *ctx, size_t index) {
    SubCallbacks *callbacks = ctx;
    *callbacks = sub_callbacks[index];
}

static GglError make_subscribe_request(
    GglBuffer interface,
    GglBuffer method,
    GglMap params,
    GglError *error,
    int *conn_fd
) {
    int conn = -1;
    GglError ret = ggl_client_send_message(
        interface, GGL_CORE_BUS_SUBSCRIBE, method, params, &conn
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    GGL_DEFER(close, conn);

    pthread_mutex_lock(&ggl_core_bus_client_payload_array_mtx);
    GGL_DEFER(pthread_mutex_unlock, ggl_core_bus_client_payload_array_mtx);

    GglBuffer recv_buffer = GGL_BUF(ggl_core_bus_client_payload_array);
    EventStreamMessage msg = { 0 };
    ret = ggl_client_get_response(
        ggl_fd_reader, &conn, recv_buffer, error, &msg
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    bool accepted = false;

    EventStreamHeaderIter iter = msg.headers;
    EventStreamHeader header;

    while (eventstream_header_next(&iter, &header) == GGL_ERR_OK) {
        if (ggl_buffer_eq(header.name, GGL_STR("accepted"))) {
            if ((header.value.type == EVENTSTREAM_INT32)
                && (header.value.int32 == 1)) {
                accepted = true;
            }
        }
    }

    if (!accepted) {
        GGL_LOGE(
            "core-bus-client",
            "Response for subscription not accepted or error."
        );
        return GGL_ERR_FAILURE;
    }

    GGL_DEFER_CANCEL(conn);
    *conn_fd = conn;
    return GGL_ERR_OK;
}

GglError ggl_subscribe(
    GglBuffer interface,
    GglBuffer method,
    GglMap params,
    GglSubscribeCallback on_response,
    GglSubscribeCloseCallback on_close,
    void *ctx,
    GglError *error,
    uint32_t *handle
) {
    if (epoll_fd < 0) {
        GGL_LOGE("core-bus-client", "Subscription epoll not initialized.");
        return GGL_ERR_FATAL;
    }

    int conn = -1;
    GglError ret
        = make_subscribe_request(interface, method, params, error, &conn);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    uint32_t sub_handle = 0;
    ret = ggl_socket_pool_register(&pool, conn, &sub_handle);
    if (ret != GGL_ERR_OK) {
        close(conn);
        return ret;
    }

    ggl_with_socket_handle_index(
        set_sub_callbacks,
        &(SubCallbacks) {
            .on_response = on_response,
            .on_close = on_close,
            .ctx = ctx,
        },
        &pool,
        sub_handle
    );

    ret = ggl_socket_epoll_add(epoll_fd, conn, sub_handle);
    if (ret != GGL_ERR_OK) {
        ggl_socket_handle_close(&pool, sub_handle);
        return ret;
    }

    if (handle != NULL) {
        *handle = sub_handle;
    }

    return GGL_ERR_OK;
}

void ggl_client_sub_close(uint32_t handle) {
    ggl_socket_handle_close(&pool, handle);
}

static GglError socket_handle_reader(void *ctx, GglBuffer buf) {
    uint32_t *handle_ptr = ctx;
    return ggl_socket_handle_read(&pool, *handle_ptr, buf);
}

static GglError get_subscription_response(uint32_t handle) {
    // Need separate data array as sub resp callback may call core bus APIs
    static uint8_t sub_resp_payload_array[GGL_COREBUS_MAX_MSG_LEN];
    static pthread_mutex_t sub_resp_payload_array_mtx
        = PTHREAD_MUTEX_INITIALIZER;

    pthread_mutex_lock(&sub_resp_payload_array_mtx);
    GGL_DEFER(pthread_mutex_unlock, sub_resp_payload_array_mtx);

    GglBuffer recv_buffer = GGL_BUF(sub_resp_payload_array);
    EventStreamMessage msg = { 0 };
    GglError ret = ggl_client_get_response(
        socket_handle_reader, &handle, recv_buffer, NULL, &msg
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    static uint8_t obj_decode_mem[PAYLOAD_MAX_SUBOBJECTS * sizeof(GglObject)];
    GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(obj_decode_mem));

    GglObject result = GGL_OBJ_NULL();
    ret = ggl_deserialize(&balloc.alloc, false, msg.payload, &result);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "core-bus-client", "Failed to decode subscription response payload."
        );
        return ret;
    }

    SubCallbacks callbacks = { 0 };
    ret = ggl_with_socket_handle_index(
        get_sub_callbacks, &callbacks, &pool, handle
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (callbacks.on_response != NULL) {
        ret = callbacks.on_response(callbacks.ctx, handle, result);
        if (ret != GGL_ERR_OK) {
            ggl_socket_handle_close(&pool, handle);
        }
    }

    return GGL_ERR_OK;
}

static GglError sub_fd_ready(void *ctx, uint64_t data) {
    (void) ctx;
    if (data > UINT32_MAX) {
        return GGL_ERR_FATAL;
    }

    uint32_t handle = (uint32_t) data;

    GglError ret = get_subscription_response(handle);
    if (ret != GGL_ERR_OK) {
        ggl_socket_handle_close(&pool, handle);
    }

    return GGL_ERR_OK;
}

static void *subscription_thread(void *args) {
    assert(epoll_fd >= 0);

    GGL_LOGD("core-bus-client", "Started core bus subscription thread.");
    ggl_socket_epoll_run(epoll_fd, sub_fd_ready, args);
    GGL_LOGE("core-bus-client", "Core bus subscription thread exited.");
    return NULL;
}
