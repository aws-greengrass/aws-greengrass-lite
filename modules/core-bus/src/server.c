// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "object_serde.h"
#include "types.h"
#include <assert.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/eventstream/decode.h>
#include <gg/eventstream/encode.h>
#include <gg/eventstream/types.h>
#include <gg/io.h>
#include <gg/log.h>
#include <gg/object.h>
#include <gg/types.h>
#include <gg/vector.h>
#include <ggl/core_bus/constants.h>
#include <ggl/core_bus/server.h>
#include <ggl/socket_handle.h>
#include <ggl/socket_server.h>
#include <pthread.h>
#include <sys/types.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PAYLOAD_VALUE_MAX_SUBOBJECTS 200

typedef struct {
    GglRpcMethodDesc *handlers;
    size_t handlers_len;
} InterfaceCtx;

typedef struct {
    GglServerSubCloseCallback fn;
    void *ctx;
} SubCleanupCallback;

static uint8_t encode_array[GGL_COREBUS_MAX_MSG_LEN];
static pthread_mutex_t encode_array_mtx = PTHREAD_MUTEX_INITIALIZER;

static GglCoreBusRequestType client_request_types[GGL_COREBUS_MAX_CLIENTS];
static SubCleanupCallback subscription_cleanup[GGL_COREBUS_MAX_CLIENTS];

static GgError reset_client_state(uint32_t handle, size_t index);
static GgError close_subscription(uint32_t handle, size_t index);

static int32_t client_fds[GGL_COREBUS_MAX_CLIENTS];
static uint16_t client_generations[GGL_COREBUS_MAX_CLIENTS];

static GglSocketPool pool = {
    .max_fds = GGL_COREBUS_MAX_CLIENTS,
    .fds = client_fds,
    .generations = client_generations,
    .on_register = reset_client_state,
    .on_release = close_subscription,
};

__attribute__((constructor)) static void init_client_pool(void) {
    ggl_socket_pool_init(&pool);
}

/// Set to a handle when calling handler.
/// ggl_sub_respond blocks if this is the response handle.
static _Atomic(uint32_t) current_handle = 0;
/// Cond var for when current_handle is cleared
static pthread_cond_t current_handle_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t current_handle_mtx = PTHREAD_MUTEX_INITIALIZER;

static inline void cleanup_socket_handle(const uint32_t *handle) {
    if (*handle != 0) {
        (void) ggl_socket_handle_close(&pool, *handle);
    }
}

static GgError reset_client_state(uint32_t handle, size_t index) {
    (void) handle;
    client_request_types[index] = GGL_CORE_BUS_CALL;
    subscription_cleanup[index].fn = NULL;
    subscription_cleanup[index].ctx = NULL;
    return GG_ERR_OK;
}

static GgError close_subscription(uint32_t handle, size_t index) {
    if (subscription_cleanup[index].fn != NULL) {
        subscription_cleanup[index].fn(subscription_cleanup[index].ctx, handle);
    }
    return GG_ERR_OK;
}

static void set_request_type(void *ctx, size_t index) {
    GglCoreBusRequestType *type = ctx;
    client_request_types[index] = *type;
}

static void get_request_type(void *ctx, size_t index) {
    GglCoreBusRequestType *type = ctx;
    *type = client_request_types[index];
}

static void set_subscription_cleanup(void *ctx, size_t index) {
    SubCleanupCallback *type = ctx;
    subscription_cleanup[index] = *type;
}

static void set_current_handle(uint32_t handle) {
    atomic_store_explicit(&current_handle, handle, memory_order_release);
}

static uint32_t get_current_handle(void) {
    return atomic_load_explicit(&current_handle, memory_order_acquire);
}

static void clear_current_handle(void) {
    GG_MTX_SCOPE_GUARD(&current_handle_mtx);
    atomic_store_explicit(&current_handle, 0, memory_order_release);
    pthread_cond_broadcast(&current_handle_cond);
}

static void wait_while_current_handle(uint32_t handle) {
    if (handle == get_current_handle()) {
        GG_MTX_SCOPE_GUARD(&current_handle_mtx);
        while (handle == get_current_handle()) {
            pthread_cond_wait(&current_handle_cond, &current_handle_mtx);
        }
    }
}

static void cleanup_current_handle(const uint32_t *handle) {
    if (*handle == get_current_handle()) {
        clear_current_handle();
    }
}

static void send_err_response(uint32_t handle, GgError error) {
    assert(error != GG_ERR_OK); // Returning error ok is invalid

    GG_MTX_SCOPE_GUARD(&encode_array_mtx);

    GgBuffer send_buffer = GG_BUF(encode_array);

    EventStreamHeader resp_headers[] = {
        { GG_STR("error"), { EVENTSTREAM_INT32, .int32 = (int32_t) error } },
    };
    size_t resp_headers_len = sizeof(resp_headers) / sizeof(resp_headers[0]);

    GgError ret = eventstream_encode(
        &send_buffer, resp_headers, resp_headers_len, GG_NULL_READER
    );

    if (ret == GG_ERR_OK) {
        (void) ggl_socket_handle_write(&pool, handle, send_buffer);
    }

    (void) ggl_socket_handle_close(&pool, handle);
}

// TODO: Split this function up
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static GgError client_ready(void *ctx, uint32_t handle) {
    GG_LOGD("Handling client data for handle %d.", handle);
    InterfaceCtx *interface = ctx;

    static pthread_mutex_t client_handler_mtx = PTHREAD_MUTEX_INITIALIZER;
    GG_MTX_SCOPE_GUARD(&client_handler_mtx);

    static uint8_t payload_array[GGL_COREBUS_MAX_MSG_LEN];

    GgBuffer recv_buffer = GG_BUF(payload_array);
    GgBuffer prelude_buf = gg_buffer_substr(recv_buffer, 0, 12);
    assert(prelude_buf.len == 12);

    GgError ret = ggl_socket_handle_read(&pool, handle, prelude_buf);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    EventStreamPrelude prelude;
    ret = eventstream_decode_prelude(prelude_buf, &prelude);
    if (ret != GG_ERR_OK) {
        send_err_response(handle, ret);
        return GG_ERR_OK;
    }

    if (prelude.data_len > recv_buffer.len) {
        GG_LOGE("EventStream packet does not fit in core bus buffer size.");
        send_err_response(handle, GG_ERR_NOMEM);
        return GG_ERR_OK;
    }

    GgBuffer data_section = gg_buffer_substr(recv_buffer, 0, prelude.data_len);

    ret = ggl_socket_handle_read(&pool, handle, data_section);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    EventStreamMessage msg;

    ret = eventstream_decode(&prelude, data_section, &msg);
    if (ret != GG_ERR_OK) {
        send_err_response(handle, ret);
        return GG_ERR_OK;
    }

    GgBuffer method = { 0 };
    bool method_set = false;
    GglCoreBusRequestType type = GGL_CORE_BUS_CALL;
    bool type_set = false;

    {
        EventStreamHeaderIter iter = msg.headers;
        EventStreamHeader header;

        while (eventstream_header_next(&iter, &header) == GG_ERR_OK) {
            if (gg_buffer_eq(header.name, GG_STR("method"))) {
                if (header.value.type != EVENTSTREAM_STRING) {
                    GG_LOGE("Method header not string.");
                    send_err_response(handle, GG_ERR_INVALID);
                    return GG_ERR_OK;
                }
                method = header.value.string;
                method_set = true;
            } else if (gg_buffer_eq(header.name, GG_STR("type"))) {
                if (header.value.type != EVENTSTREAM_INT32) {
                    GG_LOGE("Type header not int.");
                    send_err_response(handle, GG_ERR_INVALID);
                    return GG_ERR_OK;
                }
                switch (header.value.int32) {
                case GGL_CORE_BUS_NOTIFY:
                case GGL_CORE_BUS_CALL:
                case GGL_CORE_BUS_SUBSCRIBE:
                    type = (GglCoreBusRequestType) header.value.int32;
                    break;
                default:
                    GG_LOGE("Type header has invalid value.");
                    send_err_response(handle, GG_ERR_INVALID);
                    return GG_ERR_OK;
                }
                type_set = true;
            }
        }
    }

    if (!method_set || !type_set) {
        GG_LOGE("Required header missing.");
        send_err_response(handle, GG_ERR_INVALID);
        return GG_ERR_OK;
    }

    GgMap params = { 0 };

    if (msg.payload.len > 0) {
        static uint8_t payload_deserialize_mem
            [PAYLOAD_VALUE_MAX_SUBOBJECTS * sizeof(GgObject)];
        GgArena alloc = gg_arena_init(GG_BUF(payload_deserialize_mem));

        GgObject payload_obj;
        ret = ggl_deserialize(&alloc, msg.payload, &payload_obj);
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to decode request payload.");
            send_err_response(handle, ret);
            return GG_ERR_OK;
        }

        if (gg_obj_type(payload_obj) != GG_TYPE_MAP) {
            GG_LOGE("Request payload is not a map.");
            send_err_response(handle, GG_ERR_INVALID);
            return GG_ERR_OK;
        }

        params = gg_obj_into_map(payload_obj);
    }

    GG_LOGT("Setting request type.");
    ret = ggl_socket_handle_protected(set_request_type, &type, &pool, handle);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    GG_LOGD(
        "Dispatching request for method %.*s.", (int) method.len, method.data
    );

    for (size_t i = 0; i < interface->handlers_len; i++) {
        GglRpcMethodDesc *handler = &interface->handlers[i];
        if (gg_buffer_eq(method, handler->name)) {
            if (handler->is_subscription != (type == GGL_CORE_BUS_SUBSCRIBE)) {
                GG_LOGE("Request type is unsupported for method.");
                send_err_response(handle, GG_ERR_INVALID);
                return GG_ERR_OK;
            }

            set_current_handle(handle);

            ret = handler->handler(handler->ctx, params, handle);

            // Handler must either error, or succeed after calling ggl_respond
            // or ggl_sub_accept. Both of those clear current_handle
            assert(get_current_handle() == ((ret == GG_ERR_OK) ? 0 : handle));

            if (ret != GG_ERR_OK) {
                send_err_response(handle, ret);
                clear_current_handle();
            }

            return GG_ERR_OK;
        }
    }

    GG_LOGW("No handler for method %.*s.", (int) method.len, method.data);

    send_err_response(handle, GG_ERR_NOENTRY);
    return GG_ERR_OK;
}

GgError ggl_listen(
    GgBuffer interface, GglRpcMethodDesc *handlers, size_t handlers_len
) {
    uint8_t socket_path_buf
        [GGL_INTERFACE_SOCKET_PREFIX_LEN + GGL_INTERFACE_NAME_MAX_LEN]
        = GGL_INTERFACE_SOCKET_PREFIX;
    GgByteVec socket_path = { .buf = { .data = socket_path_buf,
                                       .len = GGL_INTERFACE_SOCKET_PREFIX_LEN },
                              .capacity = sizeof(socket_path_buf) };

    GgError ret = gg_byte_vec_append(&socket_path, interface);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Interface name too long.");
        return GG_ERR_RANGE;
    }

    GG_LOGD(
        "Listening on socket %.*s.",
        (int) socket_path.buf.len,
        socket_path.buf.data
    );

    InterfaceCtx ctx = { .handlers = handlers, .handlers_len = handlers_len };

    return ggl_socket_server_listen(
        &interface, socket_path.buf, 0660, &pool, client_ready, &ctx
    );
}

void ggl_respond(uint32_t handle, GgObject value) {
    GG_LOGT("Responding to %d.", handle);

    assert(handle == get_current_handle());
    GG_CLEANUP(cleanup_current_handle, handle);

    GG_LOGT("Retrieving request type for %d.", handle);
    GglCoreBusRequestType type = GGL_CORE_BUS_CALL;
    GgError ret
        = ggl_socket_handle_protected(get_request_type, &type, &pool, handle);
    if (ret != GG_ERR_OK) {
        return;
    }

    GG_CLEANUP(cleanup_socket_handle, handle);

    if (type == GGL_CORE_BUS_NOTIFY) {
        GG_LOGT("Skipping response and closing notify %d.", handle);
        return;
    }

    assert(type == GGL_CORE_BUS_CALL);

    GG_MTX_SCOPE_GUARD(&encode_array_mtx);

    GgBuffer send_buffer = GG_BUF(encode_array);

    ret = eventstream_encode(
        &send_buffer, NULL, 0, ggl_serialize_reader(&value)
    );
    if (ret != GG_ERR_OK) {
        return;
    }

    ret = ggl_socket_handle_write(&pool, handle, send_buffer);
    if (ret != GG_ERR_OK) {
        return;
    }

    GG_LOGT("Completed call response to %d.", handle);
}

void ggl_sub_accept(
    uint32_t handle, GglServerSubCloseCallback on_close, void *ctx
) {
    GG_LOGT("Accepting subscription %d.", handle);

    assert(handle == get_current_handle());
    GG_CLEANUP(cleanup_current_handle, handle);

    if (on_close != NULL) {
        SubCleanupCallback cleanup = { .fn = on_close, .ctx = ctx };

        GG_LOGT("Setting close callback for %d.", handle);
        GgError ret = ggl_socket_handle_protected(
            set_subscription_cleanup, &cleanup, &pool, handle
        );
        if (ret != GG_ERR_OK) {
            on_close(ctx, handle);
            return;
        }
    }

    GG_CLEANUP_ID(handle_cleanup, cleanup_socket_handle, handle);

    GG_MTX_SCOPE_GUARD(&encode_array_mtx);

    GgBuffer send_buffer = GG_BUF(encode_array);

    EventStreamHeader resp_headers[] = {
        { GG_STR("accepted"), { EVENTSTREAM_INT32, .int32 = 1 } },
    };
    size_t resp_headers_len = sizeof(resp_headers) / sizeof(resp_headers[0]);

    GgError ret = eventstream_encode(
        &send_buffer, resp_headers, resp_headers_len, GG_NULL_READER
    );
    if (ret != GG_ERR_OK) {
        return;
    }

    ret = ggl_socket_handle_write(&pool, handle, send_buffer);
    if (ret != GG_ERR_OK) {
        return;
    }

    // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores) false positive
    handle_cleanup = 0;
    GG_LOGT("Successfully accepted subscription %d.", handle);
}

void ggl_sub_respond(uint32_t handle, GgObject value) {
    GG_LOGT("Responding to %d.", handle);

#ifndef NDEBUG
    GglCoreBusRequestType type = GGL_CORE_BUS_CALL;
    GgError ret
        = ggl_socket_handle_protected(get_request_type, &type, &pool, handle);
    if (ret != GG_ERR_OK) {
        return;
    }
    assert(type == GGL_CORE_BUS_SUBSCRIBE);
#endif

    wait_while_current_handle(handle);

    GG_CLEANUP_ID(handle_cleanup, cleanup_socket_handle, handle);

    GG_MTX_SCOPE_GUARD(&encode_array_mtx);

    GgBuffer send_buffer = GG_BUF(encode_array);

    ret = eventstream_encode(
        &send_buffer, NULL, 0, ggl_serialize_reader(&value)
    );
    if (ret != GG_ERR_OK) {
        return;
    }

    ret = ggl_socket_handle_write(&pool, handle, send_buffer);
    if (ret != GG_ERR_OK) {
        return;
    }

    // Keep subscription handle on successful subscription response
    // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores) false positive
    handle_cleanup = 0;

    GG_LOGT("Sent response to %d.", handle);
}

void ggl_server_sub_close(uint32_t handle) {
    (void) ggl_socket_handle_close(&pool, handle);
}
