// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "client_common.h"
#include "object_serde.h"
#include "types.h"
#include <assert.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/eventstream/decode.h>
#include <gg/eventstream/types.h>
#include <gg/file.h>
#include <gg/log.h>
#include <gg/socket.h>
#include <gg/socket_epoll.h>
#include <gg/types.h>
#include <ggl/core_bus/client.h>
#include <ggl/core_bus/constants.h>
#include <ggl/socket_handle.h>
#include <pthread.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

// This must be separate C file from rest of core bus functionality as it
// creates a thread on startup using a constructor function.
// When including a .a, the linker only uses .o files that resolve a needed
// symbol. Since this is a separate .o, that means it will only be included if
// ggl_subscribe is used by the binary, and the thread is only created in
// binaries using ggl_subscribe functionality.

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

static GgError reset_sub_state(uint32_t handle, size_t index);
static GgError call_close_callback(uint32_t handle, size_t index);

static int sub_fds[GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS];
static uint16_t sub_generations[GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS];

GglSocketPool pool = {
    .max_fds = GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS,
    .fds = sub_fds,
    .generations = sub_generations,
    .on_register = reset_sub_state,
    .on_release = call_close_callback,
};

__attribute__((constructor)) static void init_sub_pool(void) {
    ggl_socket_pool_init(&pool);
}

static void *subscription_thread(void *args);

static int epoll_fd = -1;

/// Initializes subscription epoll and starts epoll thread.
/// Runs at startup (before main).
__attribute__((constructor)) static void start_subscription_thread(void) {
    GgError ret = gg_socket_epoll_create(&epoll_fd);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to create epoll for subscription responses.");
        _Exit(1);
    }

    pthread_t sub_thread = { 0 };
    int sys_ret = pthread_create(&sub_thread, NULL, subscription_thread, NULL);
    if (sys_ret != 0) {
        GG_LOGE("Failed to create subscription response thread: %d.", sys_ret);
        _Exit(1);
    }
    pthread_detach(sub_thread);
}

static GgError reset_sub_state(uint32_t handle, size_t index) {
    (void) handle;
    sub_callbacks[index] = (SubCallbacks) { 0 };
    return GG_ERR_OK;
}

static void set_sub_callbacks(void *ctx, size_t index) {
    SubCallbacks *callbacks = ctx;
    sub_callbacks[index] = *callbacks;
}

static void get_sub_callbacks(void *ctx, size_t index) {
    SubCallbacks *callbacks = ctx;
    *callbacks = sub_callbacks[index];
}

static GgError call_close_callback(uint32_t handle, size_t index) {
    (void) index;
    GG_LOGT("Calling subscription close callback.");

    GG_LOGT("Retrieving subscription callbacks.");
    SubCallbacks callbacks = { 0 };
    GgError ret = ggl_socket_handle_protected(
        get_sub_callbacks, &callbacks, &pool, handle
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    if (callbacks.on_close != NULL) {
        GG_LOGT("Calling subscription close callback.");

        callbacks.on_close(callbacks.ctx, handle);
    }

    return GG_ERR_OK;
}

static GgError make_subscribe_request(
    GgBuffer interface,
    GgBuffer method,
    GgMap params,
    GgError *error,
    int *conn_fd
) {
    int conn = -1;
    GgError ret = ggl_client_send_message(
        interface, GGL_CORE_BUS_SUBSCRIBE, method, params, &conn
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }
    GG_CLEANUP_ID(conn_cleanup, cleanup_close, conn);

    GG_MTX_SCOPE_GUARD(&ggl_core_bus_client_payload_array_mtx);

    GgBuffer recv_buffer = GG_BUF(ggl_core_bus_client_payload_array);
    EventStreamMessage msg = { 0 };
    ret = ggl_client_get_response(
        gg_socket_reader(&conn), recv_buffer, error, &msg
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    bool accepted = false;

    EventStreamHeaderIter iter = msg.headers;
    EventStreamHeader header;

    while (eventstream_header_next(&iter, &header) == GG_ERR_OK) {
        if (gg_buffer_eq(header.name, GG_STR("accepted"))) {
            if ((header.value.type == EVENTSTREAM_INT32)
                && (header.value.int32 == 1)) {
                accepted = true;
            }
        }
    }

    if (!accepted) {
        GG_LOGE("Non-error subscription response missing accepted header.");
        return GG_ERR_FAILURE;
    }

    // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores) false positive
    conn_cleanup = -1;
    *conn_fd = conn;
    return GG_ERR_OK;
}

GgError ggl_subscribe(
    GgBuffer interface,
    GgBuffer method,
    GgMap params,
    GglSubscribeCallback on_response,
    GglSubscribeCloseCallback on_close,
    void *ctx,
    GgError *error,
    uint32_t *handle
) {
    if (epoll_fd < 0) {
        GG_LOGE("Subscription epoll not initialized.");
        return GG_ERR_FATAL;
    }

    int conn = -1;
    GG_LOGT(
        "Subscribing to %.*s:%.*s.",
        (int) interface.len,
        interface.data,
        (int) method.len,
        method.data
    );
    GgError ret
        = make_subscribe_request(interface, method, params, error, &conn);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    GG_LOGT("Registering subscription fd with socket pool.");
    uint32_t sub_handle = 0;
    ret = ggl_socket_pool_register(&pool, conn, &sub_handle);
    if (ret != GG_ERR_OK) {
        (void) gg_close(conn);
        GG_LOGW("Max subscriptions exceeded.");
        return ret;
    }

    GG_LOGT("Setting subscription callbacks.");
    (void) ggl_socket_handle_protected(
        set_sub_callbacks,
        &(SubCallbacks) {
            .on_response = on_response,
            .on_close = on_close,
            .ctx = ctx,
        },
        &pool,
        sub_handle
    );

    ret = gg_socket_epoll_add(epoll_fd, conn, sub_handle);
    if (ret != GG_ERR_OK) {
        (void) ggl_socket_handle_protected(
            set_sub_callbacks, &(SubCallbacks) { 0 }, &pool, sub_handle
        );
        (void) ggl_socket_handle_close(&pool, sub_handle);
        return ret;
    }

    if (handle != NULL) {
        *handle = sub_handle;
    }

    GG_LOGT("Subscription success.");
    return GG_ERR_OK;
}

void ggl_client_sub_close(uint32_t handle) {
    (void) ggl_socket_handle_close(&pool, handle);
}

typedef struct {
    uint32_t handle;
    GgObject data;
    GgError ret;
} OnResponseCallbackArgs;

static void call_on_response_callback(void *ctx, size_t index) {
    OnResponseCallbackArgs *args = ctx;
    args->ret = GG_ERR_OK;
    if (sub_callbacks[index].on_response != NULL) {
        GG_LOGT("Calling subscription response callback.");

        args->ret = sub_callbacks[index].on_response(
            sub_callbacks[index].ctx, args->handle, args->data
        );
        if (args->ret != GG_ERR_OK) {
            (void) ggl_socket_handle_close(&pool, args->handle);

            GG_LOGT("Subscription response callback returned error.");
        }
    }
}

static GgError get_subscription_response(uint32_t handle) {
    GG_LOGD("Handling incoming subscription response.");

    // Need separate data array as sub resp callback may call core bus APIs
    static uint8_t sub_resp_payload_array[GGL_COREBUS_MAX_MSG_LEN];
    static pthread_mutex_t sub_resp_payload_array_mtx
        = PTHREAD_MUTEX_INITIALIZER;

    GG_MTX_SCOPE_GUARD(&sub_resp_payload_array_mtx);

    GgBuffer recv_buffer = GG_BUF(sub_resp_payload_array);
    EventStreamMessage msg = { 0 };
    GglSocketHandleReaderCtx reader_ctx;
    GgError ret = ggl_client_get_response(
        ggl_socket_handle_reader(&reader_ctx, &pool, handle),
        recv_buffer,
        NULL,
        &msg
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    static uint8_t obj_decode_mem[PAYLOAD_MAX_SUBOBJECTS * sizeof(GgObject)];
    GgArena alloc = gg_arena_init(GG_BUF(obj_decode_mem));

    GgObject result;
    ret = ggl_deserialize(&alloc, msg.payload, &result);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to decode subscription response payload.");
        return ret;
    }

    // User callback must not run during/after a subscription close
    OnResponseCallbackArgs args = { .handle = handle, .data = result };
    ret = ggl_socket_handle_protected(
        call_on_response_callback, &args, &pool, handle
    );
    if ((ret != GG_ERR_OK) || (args.ret != GG_ERR_OK)) {
        return ret;
    }

    GG_LOGT("Successfully handled incoming subscription response.");

    return GG_ERR_OK;
}

static GgError sub_fd_ready(void *ctx, uint64_t data) {
    (void) ctx;
    if (data > UINT32_MAX) {
        return GG_ERR_FATAL;
    }

    uint32_t handle = (uint32_t) data;

    GgError ret = get_subscription_response(handle);
    if (ret != GG_ERR_OK) {
        (void) ggl_socket_handle_close(&pool, handle);
    }

    return GG_ERR_OK;
}

static void *subscription_thread(void *args) {
    assert(epoll_fd >= 0);

    GG_LOGD("Started core bus subscription thread.");
    (void) gg_socket_epoll_run(epoll_fd, sub_fd_ready, args);
    GG_LOGE("Core bus subscription thread exited.");
    return NULL;
}
