// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/core_bus/server.h"
#include "ggl/core_bus/constants.h"
#include "object_serde.h"
#include "types.h"
#include <sys/types.h>
#include <assert.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/cleanup.h>
#include <ggl/error.h>
#include <ggl/eventstream/decode.h>
#include <ggl/eventstream/encode.h>
#include <ggl/eventstream/types.h>
#include <ggl/io.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/socket_handle.h>
#include <ggl/socket_server.h>
#include <ggl/vector.h>
#include <pthread.h>
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

static GglError reset_client_state(uint32_t handle, size_t index);
static GglError close_subscription(uint32_t handle, size_t index);

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
        ggl_socket_handle_close(&pool, *handle);
    }
}

static GglError reset_client_state(uint32_t handle, size_t index) {
    (void) handle;
    client_request_types[index] = GGL_CORE_BUS_CALL;
    subscription_cleanup[index].fn = NULL;
    subscription_cleanup[index].ctx = NULL;
    return GGL_ERR_OK;
}

static GglError close_subscription(uint32_t handle, size_t index) {
    if (subscription_cleanup[index].fn != NULL) {
        subscription_cleanup[index].fn(subscription_cleanup[index].ctx, handle);
    }
    return GGL_ERR_OK;
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
    GGL_MTX_SCOPE_GUARD(&current_handle_mtx);
    atomic_store_explicit(&current_handle, 0, memory_order_release);
    pthread_cond_broadcast(&current_handle_cond);
}

static void wait_while_current_handle(uint32_t handle) {
    if (handle == get_current_handle()) {
        GGL_MTX_SCOPE_GUARD(&current_handle_mtx);
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

static void send_err_response(uint32_t handle, GglError error) {
    assert(error != GGL_ERR_OK); // Returning error ok is invalid

    GGL_MTX_SCOPE_GUARD(&encode_array_mtx);

    GglBuffer send_buffer = GGL_BUF(encode_array);

    EventStreamHeader resp_headers[] = {
        { GGL_STR("error"), { EVENTSTREAM_INT32, .int32 = (int32_t) error } },
    };
    size_t resp_headers_len = sizeof(resp_headers) / sizeof(resp_headers[0]);

    GglError ret = eventstream_encode(
        &send_buffer, resp_headers, resp_headers_len, GGL_NULL_READER
    );

    if (ret == GGL_ERR_OK) {
        ggl_socket_handle_write(&pool, handle, send_buffer);
    }

    ggl_socket_handle_close(&pool, handle);
}

// TODO: Split this function up
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static GglError client_ready(void *ctx, uint32_t handle) {
    GGL_LOGD("Handling client data for handle %d.", handle);
    InterfaceCtx *interface = ctx;

    static pthread_mutex_t client_handler_mtx = PTHREAD_MUTEX_INITIALIZER;
    GGL_MTX_SCOPE_GUARD(&client_handler_mtx);

    static uint8_t payload_array[GGL_COREBUS_MAX_MSG_LEN];

    GglBuffer recv_buffer = GGL_BUF(payload_array);
    GglBuffer prelude_buf = ggl_buffer_substr(recv_buffer, 0, 12);
    assert(prelude_buf.len == 12);

    GglError ret = ggl_socket_handle_read(&pool, handle, prelude_buf);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    EventStreamPrelude prelude;
    ret = eventstream_decode_prelude(prelude_buf, &prelude);
    if (ret != GGL_ERR_OK) {
        send_err_response(handle, ret);
        return GGL_ERR_OK;
    }

    if (prelude.data_len > recv_buffer.len) {
        GGL_LOGE("EventStream packet does not fit in core bus buffer size.");
        send_err_response(handle, GGL_ERR_NOMEM);
        return GGL_ERR_OK;
    }

    GglBuffer data_section
        = ggl_buffer_substr(recv_buffer, 0, prelude.data_len);

    ret = ggl_socket_handle_read(&pool, handle, data_section);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    EventStreamMessage msg;

    ret = eventstream_decode(&prelude, data_section, &msg);
    if (ret != GGL_ERR_OK) {
        send_err_response(handle, ret);
        return GGL_ERR_OK;
    }

    GglBuffer method = { 0 };
    bool method_set = false;
    GglCoreBusRequestType type = GGL_CORE_BUS_CALL;
    bool type_set = false;

    {
        EventStreamHeaderIter iter = msg.headers;
        EventStreamHeader header;

        while (eventstream_header_next(&iter, &header) == GGL_ERR_OK) {
            if (ggl_buffer_eq(header.name, GGL_STR("method"))) {
                if (header.value.type != EVENTSTREAM_STRING) {
                    GGL_LOGE("Method header not string.");
                    send_err_response(handle, GGL_ERR_INVALID);
                    return GGL_ERR_OK;
                }
                method = header.value.string;
                method_set = true;
            } else if (ggl_buffer_eq(header.name, GGL_STR("type"))) {
                if (header.value.type != EVENTSTREAM_INT32) {
                    GGL_LOGE("Type header not int.");
                    send_err_response(handle, GGL_ERR_INVALID);
                    return GGL_ERR_OK;
                }
                switch (header.value.int32) {
                case GGL_CORE_BUS_NOTIFY:
                case GGL_CORE_BUS_CALL:
                case GGL_CORE_BUS_SUBSCRIBE:
                    type = (GglCoreBusRequestType) header.value.int32;
                    break;
                default:
                    GGL_LOGE("Type header has invalid value.");
                    send_err_response(handle, GGL_ERR_INVALID);
                    return GGL_ERR_OK;
                }
                type_set = true;
            }
        }
    }

    if (!method_set || !type_set) {
        GGL_LOGE("Required header missing.");
        send_err_response(handle, GGL_ERR_INVALID);
        return GGL_ERR_OK;
    }

    GglMap params = { 0 };

    if (msg.payload.len > 0) {
        static uint8_t payload_deserialize_mem
            [PAYLOAD_VALUE_MAX_SUBOBJECTS * sizeof(GglObject)];
        GglBumpAlloc balloc
            = ggl_bump_alloc_init(GGL_BUF(payload_deserialize_mem));

        GglObject payload_obj = GGL_OBJ_NULL();
        ret = ggl_deserialize(&balloc.alloc, false, msg.payload, &payload_obj);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to decode request payload.");
            send_err_response(handle, ret);
            return GGL_ERR_OK;
        }

        if (payload_obj.type != GGL_TYPE_MAP) {
            GGL_LOGE("Request payload is not a map.");
            send_err_response(handle, GGL_ERR_INVALID);
            return GGL_ERR_OK;
        }

        params = payload_obj.map;
    }

    GGL_LOGT("Setting request type.");
    ret = ggl_socket_handle_protected(set_request_type, &type, &pool, handle);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GGL_LOGD(
        "Dispatching request for method %.*s.", (int) method.len, method.data
    );

    for (size_t i = 0; i < interface->handlers_len; i++) {
        GglRpcMethodDesc *handler = &interface->handlers[i];
        if (ggl_buffer_eq(method, handler->name)) {
            if (handler->is_subscription != (type == GGL_CORE_BUS_SUBSCRIBE)) {
                GGL_LOGE("Request type is unsupported for method.");
                send_err_response(handle, GGL_ERR_INVALID);
                return GGL_ERR_OK;
            }

            set_current_handle(handle);

            ret = handler->handler(handler->ctx, params, handle);

            // Handler must either error, or succeed after calling ggl_respond
            // or ggl_sub_accept. Both of those clear current_handle
            assert(get_current_handle() == ((ret == GGL_ERR_OK) ? 0 : handle));

            if (ret != GGL_ERR_OK) {
                send_err_response(handle, ret);
                clear_current_handle();
            }

            return GGL_ERR_OK;
        }
    }

    GGL_LOGW("No handler for method %.*s.", (int) method.len, method.data);

    send_err_response(handle, GGL_ERR_NOENTRY);
    return GGL_ERR_OK;
}

GglError ggl_listen(
    GglBuffer interface, GglRpcMethodDesc *handlers, size_t handlers_len
) {
    uint8_t socket_path_buf
        [GGL_INTERFACE_SOCKET_PREFIX_LEN + GGL_INTERFACE_NAME_MAX_LEN]
        = GGL_INTERFACE_SOCKET_PREFIX;
    GglByteVec socket_path
        = { .buf = { .data = socket_path_buf,
                     .len = GGL_INTERFACE_SOCKET_PREFIX_LEN },
            .capacity = sizeof(socket_path_buf) };

    GglError ret = ggl_byte_vec_append(&socket_path, interface);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Interface name too long.");
        return GGL_ERR_RANGE;
    }

    GGL_LOGD(
        "Listening on socket %.*s.",
        (int) socket_path.buf.len,
        socket_path.buf.data
    );

    InterfaceCtx ctx = { .handlers = handlers, .handlers_len = handlers_len };

    return ggl_socket_server_listen(
        &interface, socket_path.buf, 0700, &pool, client_ready, &ctx
    );
}

void ggl_respond(uint32_t handle, GglObject value) {
    GGL_LOGT("Responding to %d.", handle);

    assert(handle == get_current_handle());
    GGL_CLEANUP(cleanup_current_handle, handle);

    GGL_LOGT("Retrieving request type for %d.", handle);
    GglCoreBusRequestType type = GGL_CORE_BUS_CALL;
    GglError ret
        = ggl_socket_handle_protected(get_request_type, &type, &pool, handle);
    if (ret != GGL_ERR_OK) {
        return;
    }

    GGL_CLEANUP(cleanup_socket_handle, handle);

    if (type == GGL_CORE_BUS_NOTIFY) {
        GGL_LOGT("Skipping response and closing notify %d.", handle);
        return;
    }

    assert(type == GGL_CORE_BUS_CALL);

    GGL_MTX_SCOPE_GUARD(&encode_array_mtx);

    GglBuffer send_buffer = GGL_BUF(encode_array);

    ret = eventstream_encode(
        &send_buffer, NULL, 0, ggl_serialize_reader(&value)
    );
    if (ret != GGL_ERR_OK) {
        return;
    }

    ret = ggl_socket_handle_write(&pool, handle, send_buffer);
    if (ret != GGL_ERR_OK) {
        return;
    }

    GGL_LOGT("Completed call response to %d.", handle);
}

void ggl_sub_accept(
    uint32_t handle, GglServerSubCloseCallback on_close, void *ctx
) {
    GGL_LOGT("Accepting subscription %d.", handle);

    assert(handle == get_current_handle());
    GGL_CLEANUP(cleanup_current_handle, handle);

    if (on_close != NULL) {
        SubCleanupCallback cleanup = { .fn = on_close, .ctx = ctx };

        GGL_LOGT("Setting close callback for %d.", handle);
        GglError ret = ggl_socket_handle_protected(
            set_subscription_cleanup, &cleanup, &pool, handle
        );
        if (ret != GGL_ERR_OK) {
            on_close(ctx, handle);
            return;
        }
    }

    GGL_CLEANUP_ID(handle_cleanup, cleanup_socket_handle, handle);

    GGL_MTX_SCOPE_GUARD(&encode_array_mtx);

    GglBuffer send_buffer = GGL_BUF(encode_array);

    EventStreamHeader resp_headers[] = {
        { GGL_STR("accepted"), { EVENTSTREAM_INT32, .int32 = 1 } },
    };
    size_t resp_headers_len = sizeof(resp_headers) / sizeof(resp_headers[0]);

    GglError ret = eventstream_encode(
        &send_buffer, resp_headers, resp_headers_len, GGL_NULL_READER
    );
    if (ret != GGL_ERR_OK) {
        return;
    }

    ret = ggl_socket_handle_write(&pool, handle, send_buffer);
    if (ret != GGL_ERR_OK) {
        return;
    }

    // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores) false positive
    handle_cleanup = 0;
    GGL_LOGT("Successfully accepted subscription %d.", handle);
}

void ggl_sub_respond(uint32_t handle, GglObject value) {
    GGL_LOGT("Responding to %d.", handle);

#ifndef NDEBUG
    GglCoreBusRequestType type = GGL_CORE_BUS_CALL;
    GglError ret
        = ggl_socket_handle_protected(get_request_type, &type, &pool, handle);
    if (ret != GGL_ERR_OK) {
        return;
    }
    assert(type == GGL_CORE_BUS_SUBSCRIBE);
#endif

    wait_while_current_handle(handle);

    GGL_CLEANUP_ID(handle_cleanup, cleanup_socket_handle, handle);

    GGL_MTX_SCOPE_GUARD(&encode_array_mtx);

    GglBuffer send_buffer = GGL_BUF(encode_array);

    ret = eventstream_encode(
        &send_buffer, NULL, 0, ggl_serialize_reader(&value)
    );
    if (ret != GGL_ERR_OK) {
        return;
    }

    ret = ggl_socket_handle_write(&pool, handle, send_buffer);
    if (ret != GGL_ERR_OK) {
        return;
    }

    // Keep subscription handle on successful subscription response
    // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores) false positive
    handle_cleanup = 0;

    GGL_LOGT("Sent response to %d.", handle);
}

void ggl_server_sub_close(uint32_t handle) {
    ggl_socket_handle_close(&pool, handle);
}
