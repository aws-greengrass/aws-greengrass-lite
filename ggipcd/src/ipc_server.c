// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ipc_server.h"
#include "ipc_handler.h"
#include <assert.h>
#include <errno.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/eventstream/decode.h>
#include <ggl/eventstream/encode.h>
#include <ggl/eventstream/types.h>
#include <ggl/json_decode.h>
#include <ggl/json_encode.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/socket_handle.h>
#include <ggl/socket_server.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/// Maximum number of GG IPC clients.
/// Can be configured with `-DGGL_IPC_MAX_CLIENTS=<N>`.
#ifndef GGL_IPC_MAX_CLIENTS
#define GGL_IPC_MAX_CLIENTS 50
#endif

static_assert(
    GGL_IPC_MAX_MSG_LEN >= 16, "Minimum EventStream packet size is 16."
);

typedef enum {
    ES_APPLICATION_MESSAGE = 0,
    ES_APPLICATION_ERROR = 1,
    ES_CONNECT = 4,
    ES_CONNECT_ACK = 5,
} EsMessageType;

typedef enum {
    ES_CONNECTION_ACCEPTED = 1,
    ES_TERMINATE_STREAM = 2,
} EsMessageFlags;

typedef struct {
    int32_t stream_id;
    int32_t message_type;
    int32_t message_flags;
} EsCommonHeaders;

static const int32_t ES_FLAGS_MASK = 3;

static uint8_t payload_array[GGL_IPC_MAX_MSG_LEN];

typedef enum {
    IPC_INIT,
    IPC_CONNECTED,
} IpcConnState;

static IpcConnState client_states[GGL_IPC_MAX_CLIENTS] = { 0 };

static int32_t client_fds[GGL_IPC_MAX_CLIENTS];
static uint16_t client_generations[GGL_IPC_MAX_CLIENTS];

static GglError reset_client_state(uint32_t handle, size_t index);
static GglError release_subscriptions_for_conn(uint32_t handle, size_t index);

static GglSocketPool pool = {
    .max_fds = GGL_IPC_MAX_CLIENTS,
    .fds = client_fds,
    .generations = client_generations,
    .on_register = reset_client_state,
    .on_release = release_subscriptions_for_conn,
};

__attribute__((constructor)) static void init_client_pool(void) {
    ggl_socket_pool_init(&pool);
}

static GglIpcSubscriptionCtx
    subscription_ctx_array[GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS];
static pthread_mutex_t subscription_ctx_mtx = PTHREAD_MUTEX_INITIALIZER;

static GglError reset_client_state(uint32_t handle, size_t index) {
    (void) handle;
    client_states[index] = IPC_INIT;
    return GGL_ERR_OK;
}

static GglError get_common_headers(
    EventStreamMessage *msg, EsCommonHeaders *out
) {
    int32_t message_type = -1;
    int32_t message_flags = 0;
    int32_t stream_id = 0;

    EventStreamHeaderIter iter = msg->headers;
    EventStreamHeader header;

    while (eventstream_header_next(&iter, &header) == GGL_ERR_OK) {
        if (ggl_buffer_eq(header.name, GGL_STR(":message-type"))) {
            if (header.value.type != EVENTSTREAM_INT32) {
                GGL_LOGE("ipc-server", ":message-type header not Int32.");
                return GGL_ERR_INVALID;
            }
            message_type = header.value.int32;
        } else if (ggl_buffer_eq(header.name, GGL_STR(":message-flags"))) {
            if (header.value.type != EVENTSTREAM_INT32) {
                GGL_LOGE("ipc-server", ":message-flags header not Int32.");
                return GGL_ERR_INVALID;
            }
            message_flags = header.value.int32;
        } else if (ggl_buffer_eq(header.name, GGL_STR(":stream-id"))) {
            if (header.value.type != EVENTSTREAM_INT32) {
                GGL_LOGE("ipc-server", ":stream-id header not Int32.");
                return GGL_ERR_INVALID;
            }
            stream_id = header.value.int32;
        }
    }

    out->message_type = message_type;
    out->message_flags = message_flags;
    out->stream_id = stream_id;
    return GGL_ERR_OK;
}

static GglError deserialize_payload(GglBuffer payload, GglMap *out) {
    GglObject obj;

    static uint8_t
        json_decode_mem[GGL_IPC_PAYLOAD_MAX_SUBOBJECTS * sizeof(GglObject)];
    GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(json_decode_mem));

    GglError ret = ggl_json_decode_destructive(payload, &balloc.alloc, &obj);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("ipc-server", "Failed to decode msg payload.");
        return ret;
    }

    if (obj.type != GGL_TYPE_MAP) {
        GGL_LOGE("ipc-server", "Message payload is not a JSON object.");
        return GGL_ERR_INVALID;
    }

    *out = obj.map;
    return GGL_ERR_OK;
}

static GglError payload_writer(GglBuffer *buf, void *payload) {
    GglObject *obj = payload;

    if (obj == NULL) {
        buf->len = 0;
        return GGL_ERR_OK;
    }

    return ggl_json_encode(*obj, buf);
}

static void set_connected(void *ctx, size_t index) {
    (void) ctx;
    client_states[index] = IPC_CONNECTED;
}

static GglError handle_conn_init(uint32_t handle, EventStreamMessage *msg) {
    GGL_LOGD("ipc-server", "Handling connect for %d.", handle);

    EsCommonHeaders common_headers;
    GglError ret = get_common_headers(msg, &common_headers);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (common_headers.message_type != ES_CONNECT) {
        GGL_LOGE("ipc-server", "Client initial message not of type connect.");
        return GGL_ERR_INVALID;
    }
    if (common_headers.stream_id != 0) {
        GGL_LOGE("ipc-server", "Connect message has non-zero :stream-id.");
        return GGL_ERR_INVALID;
    }
    if ((common_headers.message_flags & ES_FLAGS_MASK) != 0) {
        GGL_LOGE("ipc-server", "Connect message has flags set.");
        return GGL_ERR_INVALID;
    }

    {
        EventStreamHeaderIter iter = msg->headers;
        EventStreamHeader header;

        while (eventstream_header_next(&iter, &header) == GGL_ERR_OK) {
            if (ggl_buffer_eq(header.name, GGL_STR(":version"))) {
                if (header.value.type != EVENTSTREAM_STRING) {
                    GGL_LOGE("ipc-server", ":version header not string.");
                    return GGL_ERR_INVALID;
                }
                if (!ggl_buffer_eq(header.value.string, GGL_STR("0.1.0"))) {
                    GGL_LOGE(
                        "ipc-server", "Client protocol version not 0.1.0."
                    );
                    return GGL_ERR_INVALID;
                }
            }
        }
    }

    GglMap payload_data = { 0 };
    ret = deserialize_payload(msg->payload, &payload_data);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GglObject *value;
    bool found = ggl_map_get(payload_data, GGL_STR("authToken"), &value);
    if (!found) {
        GGL_LOGE("ipc-server", "Connect message payload missing authToken.");
        return GGL_ERR_INVALID;
    }
    if (value->type != GGL_TYPE_BUF) {
        GGL_LOGE("ipc-server", "Connect message authToken not a string.");
        return GGL_ERR_INVALID;
    }
    GglBuffer auth_token = value->buf;

    GGL_LOGD(
        "ipc-server",
        "Client connected with token %.*s.",
        (int) auth_token.len,
        auth_token.data
    );

    GglBuffer send_buffer = GGL_BUF(payload_array);

    eventstream_encode(
        &send_buffer,
        (EventStreamHeader[]) {
            { GGL_STR(":message-type"),
              { EVENTSTREAM_INT32, .int32 = ES_CONNECT_ACK } },
            { GGL_STR(":message-flags"),
              { EVENTSTREAM_INT32, .int32 = ES_CONNECTION_ACCEPTED } },
            { GGL_STR(":stream-id"), { EVENTSTREAM_INT32, .int32 = 0 } },
        },
        3,
        payload_writer,
        NULL
    );

    ret = ggl_socket_handle_write(&pool, handle, send_buffer);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GGL_LOGT("ipc-server", "Setting %d as connected.", handle);

    ret = ggl_with_socket_handle_index(set_connected, NULL, &pool, handle);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GGL_LOGT("ipc-server", "Successfully connected %d.", handle);
    return GGL_ERR_OK;
}

static GglError handle_operation(uint32_t handle, EventStreamMessage *msg) {
    EsCommonHeaders common_headers;
    GglError ret = get_common_headers(msg, &common_headers);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (common_headers.message_type != ES_APPLICATION_MESSAGE) {
        GGL_LOGE("ipc-server", "Client sent unhandled message type.");
        return GGL_ERR_INVALID;
    }
    if (common_headers.stream_id == 0) {
        GGL_LOGE("ipc-server", "Application message has zero :stream-id.");
        return GGL_ERR_INVALID;
    }
    if ((common_headers.message_flags & ES_FLAGS_MASK) != 0) {
        GGL_LOGE("ipc-server", "Client request has flags set.");
        return GGL_ERR_INVALID;
    }

    GglBuffer operation = { 0 };

    {
        bool operation_set = false;
        EventStreamHeaderIter iter = msg->headers;
        EventStreamHeader header;

        while (eventstream_header_next(&iter, &header) == GGL_ERR_OK) {
            if (ggl_buffer_eq(header.name, GGL_STR("operation"))) {
                if (header.value.type != EVENTSTREAM_STRING) {
                    GGL_LOGE("ipc-server", "operation header not string.");
                    return GGL_ERR_INVALID;
                }
                operation = header.value.string;
                operation_set = true;
            }
        }

        if (!operation_set) {
            GGL_LOGE("ipc-server", "Client request missing operation header.");
            return GGL_ERR_INVALID;
        }
    }

    GglMap payload_data = { 0 };
    ret = deserialize_payload(msg->payload, &payload_data);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return ggl_ipc_handle_operation(
        operation, payload_data, handle, common_headers.stream_id
    );
}

static void get_conn_state(void *ctx, size_t index) {
    IpcConnState *state = ctx;
    *state = client_states[index];
}

static GglError client_ready(void *ctx, uint32_t handle) {
    (void) ctx;

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
        return ret;
    }

    if (prelude.data_len > recv_buffer.len) {
        GGL_LOGE(
            "ipc-server",
            "EventStream packet does not fit in configured IPC buffer size."
        );
        return ENOMEM;
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
        return ret;
    }

    GGL_LOGT("ipc-server", "Retrieving connection state for %d.", handle);
    IpcConnState state = IPC_INIT;
    ret = ggl_with_socket_handle_index(get_conn_state, &state, &pool, handle);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    switch (state) {
    case IPC_INIT:
        return handle_conn_init(handle, &msg);
    case IPC_CONNECTED:
        return handle_operation(handle, &msg);
    }

    assert(false);
    return GGL_ERR_FAILURE;
}

GglError ggl_ipc_listen(const char *socket_path) {
    return ggl_socket_server_listen(socket_path, &pool, client_ready, NULL);
}

GglError ggl_ipc_response_send(
    uint32_t handle,
    int32_t stream_id,
    GglBuffer service_model_type,
    GglObject response
) {
    static uint8_t resp_array[GGL_IPC_MAX_MSG_LEN];
    static pthread_mutex_t resp_array_mtx = PTHREAD_MUTEX_INITIALIZER;

    pthread_mutex_lock(&resp_array_mtx);
    GGL_DEFER(pthread_mutex_unlock, resp_array_mtx);
    GglBuffer resp_buffer = GGL_BUF(resp_array);

    EventStreamHeader resp_headers[] = {
        { GGL_STR(":message-type"),
          { EVENTSTREAM_INT32, .int32 = ES_APPLICATION_MESSAGE } },
        { GGL_STR(":message-flags"), { EVENTSTREAM_INT32, .int32 = 0 } },
        { GGL_STR(":stream-id"), { EVENTSTREAM_INT32, .int32 = stream_id } },

        { GGL_STR(":content-type"),
          { EVENTSTREAM_STRING, .string = GGL_STR("application/json") } },
        { GGL_STR("service-model-type"),
          { EVENTSTREAM_STRING, .string = service_model_type } },
    };
    const size_t RESP_HEADERS_LEN
        = sizeof(resp_headers) / sizeof(resp_headers[0]);

    GglError ret = eventstream_encode(
        &resp_buffer, resp_headers, RESP_HEADERS_LEN, payload_writer, &response
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return ggl_socket_handle_write(&pool, handle, resp_buffer);
}

GglError ggl_ipc_get_subscription_ctx(
    GglIpcSubscriptionCtx **ctx, uint32_t resp_handle
) {
    assert(resp_handle != 0);

    pthread_mutex_lock(&subscription_ctx_mtx);
    GGL_DEFER(pthread_mutex_unlock, subscription_ctx_mtx);

    for (size_t i = 0; i <= GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS; i++) {
        if (subscription_ctx_array[i].resp_handle == 0) {
            subscription_ctx_array[i].resp_handle = resp_handle;
            *ctx = &subscription_ctx_array[i];
            return GGL_ERR_OK;
        }
    }

    GGL_LOGE("ipc-server", "Exceeded maximum tracked subscriptions.");
    return GGL_ERR_NOMEM;
}

void ggl_ipc_release_subscription_ctx(GglIpcSubscriptionCtx *ctx) {
    GglIpcSubscriptionCtx *sub_ctx = ctx;

    pthread_mutex_lock(&subscription_ctx_mtx);
    GGL_DEFER(pthread_mutex_unlock, subscription_ctx_mtx);

    *sub_ctx = (GglIpcSubscriptionCtx) { 0 };
}

static GglError release_subscriptions_for_conn(uint32_t handle, size_t index) {
    (void) index; // This is pool index, not in subscription array

    for (size_t i = 0; i < GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS; i++) {
        uint32_t recv_handle = 0;

        {
            pthread_mutex_lock(&subscription_ctx_mtx);
            GGL_DEFER(pthread_mutex_unlock, subscription_ctx_mtx);

            if (subscription_ctx_array[i].resp_handle == handle) {
                recv_handle = subscription_ctx_array[i].recv_handle;
            }
        }

        if (recv_handle != 0) {
            ggl_client_sub_close(recv_handle);
        }
    }

    return GGL_ERR_OK;
}
