/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ggl/core_bus/server.h"
#include "object_serde.h"
#include "types.h"
#include <assert.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/eventstream/decode.h>
#include <ggl/eventstream/encode.h>
#include <ggl/eventstream/types.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/socket_handle.h>
#include <ggl/socket_server.h>
#include <pthread.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Maximum number of core-bus connections.
 * Can be configured with `-DGGL_COREBUS_MAX_CLIENTS=<N>`. */
#ifndef GGL_COREBUS_MAX_CLIENTS
#define GGL_COREBUS_MAX_CLIENTS 100
#endif

#define PAYLOAD_VALUE_MAX_SUBOBJECTS 50

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

// TODO: Split this function up
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static GglError client_ready(void *ctx, uint32_t handle) {
    InterfaceCtx *interface = ctx;

    static pthread_mutex_t client_handler_mtx = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&client_handler_mtx);
    GGL_DEFER(pthread_mutex_unlock, client_handler_mtx);

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
        ggl_return_err(handle, ret);
        return GGL_ERR_OK;
    }

    if (prelude.data_len > recv_buffer.len) {
        GGL_LOGE(
            "core-bus-server",
            "EventStream packet does not fit in core bus buffer size."
        );
        ggl_return_err(handle, GGL_ERR_NOMEM);
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
        ggl_return_err(handle, ret);
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
                    GGL_LOGE("core-bus-server", "Method header not string.");
                    ggl_return_err(handle, GGL_ERR_INVALID);
                    return GGL_ERR_OK;
                }
                method = header.value.string;
                method_set = true;
            } else if (ggl_buffer_eq(header.name, GGL_STR("type"))) {
                if (header.value.type != EVENTSTREAM_INT32) {
                    GGL_LOGE("core-bus-server", "Type header not int.");
                    ggl_return_err(handle, GGL_ERR_INVALID);
                    return GGL_ERR_OK;
                }
                switch (header.value.int32) {
                case GGL_CORE_BUS_NOTIFY:
                case GGL_CORE_BUS_CALL:
                case GGL_CORE_BUS_SUBSCRIBE:
                    type = (GglCoreBusRequestType) header.value.int32;
                    break;
                default:
                    GGL_LOGE(
                        "core-bus-server", "Type header has invalid value."
                    );
                    ggl_return_err(handle, GGL_ERR_INVALID);
                    return GGL_ERR_OK;
                }
                type_set = true;
            }
        }
    }

    if (!method_set || !type_set) {
        GGL_LOGE("core-bus-server", "Required header missing.");
        ggl_return_err(handle, GGL_ERR_INVALID);
        return GGL_ERR_OK;
    }

    GglMap params = GGL_MAP();

    if (msg.payload.len > 0) {
        static uint8_t payload_deserialize_mem
            [PAYLOAD_VALUE_MAX_SUBOBJECTS * sizeof(GglObject)];
        GglBumpAlloc balloc
            = ggl_bump_alloc_init(GGL_BUF(payload_deserialize_mem));

        GglObject payload_obj = GGL_OBJ_NULL();
        ret = ggl_deserialize(&balloc.alloc, false, msg.payload, &payload_obj);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("core-bus-server", "Failed to decode request payload.");
            ggl_return_err(handle, ret);
            return GGL_ERR_OK;
        }

        if (payload_obj.type != GGL_TYPE_MAP) {
            GGL_LOGE("core-bus-server", "Request payload is not a map.");
            ggl_return_err(handle, GGL_ERR_INVALID);
            return GGL_ERR_OK;
        }

        params = payload_obj.map;
    }

    ret = ggl_with_socket_handle_index(set_request_type, &type, &pool, handle);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GGL_LOGD(
        "core-bus-server",
        "Dispatching request for method %.*s.",
        (int) method.len,
        method.data
    );

    for (size_t i = 0; i < interface->handlers_len; i++) {
        GglRpcMethodDesc *handler = &interface->handlers[i];
        if (ggl_buffer_eq(method, handler->name)) {
            if (handler->is_subscription != (type == GGL_CORE_BUS_SUBSCRIBE)) {
                GGL_LOGE(
                    "core-bus-server", "Request type is unsupported for method."
                );
                ggl_return_err(handle, GGL_ERR_INVALID);
                return GGL_ERR_OK;
            }

            handler->handler(handler->ctx, params, handle);
            return GGL_ERR_OK;
        }
    }

    ggl_return_err(handle, GGL_ERR_NOENTRY);
    return GGL_ERR_OK;
}

GglError ggl_listen(
    GglBuffer interface, GglRpcMethodDesc *handlers, size_t handlers_len
) {
    char socket_path
        [GGL_INTERFACE_SOCKET_PREFIX_LEN + GGL_INTERFACE_NAME_MAX_LEN + 1]
        = GGL_INTERFACE_SOCKET_PREFIX;

    if (interface.len > GGL_INTERFACE_NAME_MAX_LEN) {
        GGL_LOGE("core-bus", "Interface name too long.");
        return GGL_ERR_RANGE;
    }

    memcpy(
        &socket_path[GGL_INTERFACE_SOCKET_PREFIX_LEN],
        interface.data,
        interface.len
    );

    InterfaceCtx ctx = { .handlers = handlers, .handlers_len = handlers_len };

    return ggl_socket_server_listen(socket_path, &pool, client_ready, &ctx);
}

static GglError payload_writer(GglBuffer *buf, void *payload) {
    GglObject *obj = payload;

    if (obj == NULL) {
        buf->len = 0;
        return GGL_ERR_OK;
    }

    return ggl_serialize(*obj, buf);
}

void ggl_return_err(uint32_t handle, GglError error) {
    pthread_mutex_lock(&encode_array_mtx);
    GGL_DEFER(pthread_mutex_unlock, encode_array_mtx);

    GglBuffer send_buffer = GGL_BUF(encode_array);

    EventStreamHeader resp_headers[] = {
        { GGL_STR("error"), { EVENTSTREAM_INT32, .int32 = (int32_t) error } },
    };
    size_t resp_headers_len = sizeof(resp_headers) / sizeof(resp_headers[0]);

    GglError ret = eventstream_encode(
        &send_buffer, resp_headers, resp_headers_len, payload_writer, NULL
    );

    if (ret == GGL_ERR_OK) {
        ggl_socket_handle_write(&pool, handle, send_buffer);
    }

    ggl_socket_handle_close(&pool, handle);
}

void ggl_respond(uint32_t handle, GglObject value) {
    GglCoreBusRequestType type = GGL_CORE_BUS_CALL;
    GglError ret
        = ggl_with_socket_handle_index(get_request_type, &type, &pool, handle);
    if (ret != GGL_ERR_OK) {
        return;
    }

    if (type == GGL_CORE_BUS_NOTIFY) {
        ggl_socket_handle_close(&pool, handle);
        return;
    }

    pthread_mutex_lock(&encode_array_mtx);
    GGL_DEFER(pthread_mutex_unlock, encode_array_mtx);

    GglBuffer send_buffer = GGL_BUF(encode_array);

    EventStreamHeader resp_headers[] = {};
    size_t resp_headers_len = sizeof(resp_headers) / sizeof(resp_headers[0]);

    ret = eventstream_encode(
        &send_buffer, resp_headers, resp_headers_len, payload_writer, &value
    );
    if (ret != GGL_ERR_OK) {
        ggl_socket_handle_close(&pool, handle);
        return;
    }

    ret = ggl_socket_handle_write(&pool, handle, send_buffer);

    if ((ret != GGL_ERR_OK) || (type != GGL_CORE_BUS_SUBSCRIBE)) {
        ggl_socket_handle_close(&pool, handle);
    }
}

void ggl_sub_accept(
    uint32_t handle, GglServerSubCloseCallback on_close, void *ctx
) {
    SubCleanupCallback cleanup = { .fn = on_close, .ctx = ctx };

    GglError ret = ggl_with_socket_handle_index(
        set_subscription_cleanup, &cleanup, &pool, handle
    );
    if (ret != GGL_ERR_OK) {
        on_close(ctx, handle);
        return;
    }

    pthread_mutex_lock(&encode_array_mtx);
    GGL_DEFER(pthread_mutex_unlock, encode_array_mtx);

    GglBuffer send_buffer = GGL_BUF(encode_array);

    EventStreamHeader resp_headers[] = {
        { GGL_STR("accepted"), { EVENTSTREAM_INT32, .int32 = 1 } },
    };
    size_t resp_headers_len = sizeof(resp_headers) / sizeof(resp_headers[0]);

    ret = eventstream_encode(
        &send_buffer, resp_headers, resp_headers_len, payload_writer, NULL
    );
    if (ret != GGL_ERR_OK) {
        ggl_socket_handle_close(&pool, handle);
    }

    ret = ggl_socket_handle_write(&pool, handle, send_buffer);
    if (ret != GGL_ERR_OK) {
        ggl_socket_handle_close(&pool, handle);
    }
}

void ggl_server_sub_close(uint32_t handle) {
    ggl_socket_handle_close(&pool, handle);
}
