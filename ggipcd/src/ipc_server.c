/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ipc_server.h"
#include "ipc_handler.h"
#include <assert.h>
#include <errno.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/error.h>
#include <ggl/eventstream/decode.h>
#include <ggl/eventstream/encode.h>
#include <ggl/eventstream/types.h>
#include <ggl/json_decode.h>
#include <ggl/json_encode.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/socket_server.h>
#include <ggl/socket_utils.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Maximum size of eventstream packet.
 * Can be configured with `-DGGL_IPC_MAX_MSG_LEN=<N>`. */
#ifndef GGL_IPC_MAX_MSG_LEN
#define GGL_IPC_MAX_MSG_LEN 10000
#endif

/** Maximum number of GG IPC clients.
 * Can be configured with `-DGGL_IPC_MAX_CLIENTS=<N>`. */
#ifndef GGL_IPC_MAX_CLIENTS
#define GGL_IPC_MAX_CLIENTS 50
#endif

#define PAYLOAD_JSON_MAX_SUBOBJECTS 50

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

static const int32_t ES_FLAGS_MASK = 3;

static uint8_t payload_array[GGL_IPC_MAX_MSG_LEN];

typedef enum {
    IPC_UNUSED,
    IPC_INIT,
    IPC_CONNECTED,
} IpcConnState;

static int client_fds[GGL_IPC_MAX_CLIENTS] = { 0 };
static IpcConnState client_states[GGL_IPC_MAX_CLIENTS] = { 0 };

typedef struct {
    int32_t stream_id;
    int32_t message_type;
    int32_t message_flags;
} EsCommonHeaders;

static bool register_client_fd(void *ctx, int fd, uint32_t *token) {
    (void) ctx;
    for (uint32_t i = 0; i < GGL_IPC_MAX_CLIENTS; i++) {
        if (client_states[i] == IPC_UNUSED) {
            client_states[i] = IPC_INIT;
            client_fds[i] = fd;
            *token = i;
            return true;
        }
    }
    return false;
}

static bool release_client_fd(void *ctx, uint32_t token, int *fd) {
    (void) ctx;
    *fd = client_fds[token];
    client_fds[token] = -1;
    client_states[token] = IPC_UNUSED;
    return true;
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
        json_decode_mem[PAYLOAD_JSON_MAX_SUBOBJECTS * sizeof(GglObject)];
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

static GglError handle_conn_init(
    int fd, IpcConnState *state, EventStreamMessage *msg
) {
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

    ret = socket_write(fd, send_buffer);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    *state = IPC_CONNECTED;
    return GGL_ERR_OK;
}

static GglError handle_operation(int fd, EventStreamMessage *msg) {
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

    static uint8_t resp_mem[PAYLOAD_JSON_MAX_SUBOBJECTS * sizeof(GglObject)];
    GglBumpAlloc buff_alloc = ggl_bump_alloc_init(GGL_BUF(resp_mem));

    GglBuffer resp_service_model_type = { 0 };
    GglObject resp_obj = { 0 };

    ret = ggl_ipc_handle_operation(
        operation,
        payload_data,
        &buff_alloc.alloc,
        &resp_service_model_type,
        &resp_obj
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GglBuffer send_buffer = GGL_BUF(payload_array);

    EventStreamHeader resp_headers[] = {
        { GGL_STR(":message-type"),
          { EVENTSTREAM_INT32, .int32 = ES_APPLICATION_MESSAGE } },
        { GGL_STR(":message-flags"), { EVENTSTREAM_INT32, .int32 = 0 } },
        { GGL_STR(":stream-id"),
          { EVENTSTREAM_INT32, .int32 = common_headers.stream_id } },
        { GGL_STR(":content-type"),
          { EVENTSTREAM_STRING, .string = GGL_STR("application/json") } },
        { GGL_STR("service-model-type"),
          { EVENTSTREAM_STRING, .string = resp_service_model_type } },
    };
    size_t resp_headers_len = sizeof(resp_headers) / sizeof(resp_headers[0]);

    eventstream_encode(
        &send_buffer, resp_headers, resp_headers_len, payload_writer, &resp_obj
    );

    return socket_write(fd, send_buffer);
}

static GglError data_ready(void *ctx, uint32_t token) {
    (void) ctx;

    GglBuffer recv_buffer = GGL_BUF(payload_array);
    GglBuffer prelude_buf = ggl_buffer_substr(recv_buffer, 0, 12);
    assert(prelude_buf.len == 12);

    int fd = client_fds[token];

    GglError ret = socket_read(fd, prelude_buf);
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

    ret = socket_read(fd, data_section);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    EventStreamMessage msg;

    ret = eventstream_decode(&prelude, data_section, &msg);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    switch (client_states[token]) {
    case IPC_UNUSED:
        GGL_LOGE("ipc-server", "IPC server connection state bad.");
        return GGL_ERR_FAILURE;
    case IPC_INIT:
        return handle_conn_init(fd, &client_states[token], &msg);
    case IPC_CONNECTED:
        return handle_operation(fd, &msg);
    }

    assert(false);
    return GGL_ERR_FAILURE;
}

GglError ggl_ipc_listen(const char *socket_path) {
    return ggl_socket_server_listen(
        socket_path, register_client_fd, release_client_fd, data_ready, NULL
    );
}
