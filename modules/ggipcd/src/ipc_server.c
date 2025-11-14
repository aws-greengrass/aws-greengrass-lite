// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ipc_server.h"
#include "ipc_components.h"
#include "ipc_dispatch.h"
#include "ipc_error.h"
#include "ipc_subscriptions.h"
#include <assert.h>
#include <gg/arena.h>
#include <gg/base64.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/eventstream/decode.h>
#include <gg/eventstream/encode.h>
#include <gg/eventstream/rpc.h>
#include <gg/eventstream/types.h>
#include <gg/flags.h>
#include <gg/io.h>
#include <gg/ipc/limits.h>
#include <gg/json_decode.h>
#include <gg/json_encode.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <ggipc/auth.h>
#include <ggl/socket_handle.h>
#include <ggl/socket_server.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>

/// Maximum number of GG IPC clients.
/// Can be configured with `-DGGL_IPC_MAX_CLIENTS=<N>`.
#ifndef GGL_IPC_MAX_CLIENTS
#define GGL_IPC_MAX_CLIENTS 50
#endif

static_assert(
    GGL_IPC_MAX_MSG_LEN >= 16, "Minimum EventStream packet size is 16."
);

static uint8_t resp_array[GGL_IPC_MAX_MSG_LEN];
static pthread_mutex_t resp_array_mtx = PTHREAD_MUTEX_INITIALIZER;

static GglComponentHandle client_components[GGL_IPC_MAX_CLIENTS];

static GgError reset_client_state(uint32_t handle, size_t index);
static GgError release_client_subscriptions(uint32_t handle, size_t index);

static GglSocketPool pool = {
    .max_fds = GGL_IPC_MAX_CLIENTS,
    .fds = (int32_t[GGL_IPC_MAX_CLIENTS]) { 0 },
    .generations = (uint16_t[GGL_IPC_MAX_CLIENTS]) { 0 },
    .on_register = reset_client_state,
    .on_release = release_client_subscriptions,
};

__attribute__((constructor)) static void init_client_pool(void) {
    ggl_socket_pool_init(&pool);
}

static GgError reset_client_state(uint32_t handle, size_t index) {
    (void) handle;
    client_components[index] = 0;
    return GG_ERR_OK;
}

static GgError release_client_subscriptions(uint32_t handle, size_t index) {
    (void) index;
    return ggl_ipc_release_subscriptions_for_conn(handle);
}

static GgError deserialize_payload(
    GgBuffer payload, GgMap *out, GgArena *alloc
) {
    GgObject obj;

    GG_LOGT(
        "Deserializing payload %.*s", (int) payload.len, (char *) payload.data
    );

    GgError ret = gg_json_decode_destructive(payload, alloc, &obj);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to decode msg payload.");
        return ret;
    }

    if (gg_obj_type(obj) != GG_TYPE_MAP) {
        GG_LOGE("Message payload is not a JSON object.");
        return GG_ERR_INVALID;
    }

    *out = gg_obj_into_map(obj);
    return GG_ERR_OK;
}

static void set_conn_component(void *ctx, size_t index) {
    GglComponentHandle *component_handle = ctx;
    assert(*component_handle != 0);

    client_components[index] = *component_handle;
}

static GgError validate_conn_msg(
    EventStreamMessage *msg, EventStreamCommonHeaders common_headers
) {
    if (common_headers.message_type != EVENTSTREAM_CONNECT) {
        GG_LOGE("Client initial message not of type connect.");
        return GG_ERR_INVALID;
    }
    if (common_headers.stream_id != 0) {
        GG_LOGE("Connect message has non-zero :stream-id.");
        return GG_ERR_INVALID;
    }
    if ((common_headers.message_flags & EVENTSTREAM_FLAGS_MASK) != 0) {
        GG_LOGE("Connect message has flags set.");
        return GG_ERR_INVALID;
    }

    EventStreamHeaderIter iter = msg->headers;
    EventStreamHeader header;

    while (eventstream_header_next(&iter, &header) == GG_ERR_OK) {
        if (gg_buffer_eq(header.name, GG_STR(":version"))) {
            if (header.value.type != EVENTSTREAM_STRING) {
                GG_LOGE(":version header not string.");
                return GG_ERR_INVALID;
            }
            if (!gg_buffer_eq(header.value.string, GG_STR("0.1.0"))) {
                GG_LOGE("Client protocol version not 0.1.0.");
                return GG_ERR_INVALID;
            }
        }
    }

    return GG_ERR_OK;
}

static GgError send_conn_resp(uint32_t handle, GglSvcuid *svcuid) {
    GG_MTX_SCOPE_GUARD(&resp_array_mtx);
    GgBuffer resp_buffer = GG_BUF(resp_array);

    uint8_t svcuid_mem[GG_IPC_SVCUID_STR_LEN];
    GgBuffer svcuid_str = GG_STR("");

    if (svcuid != NULL) {
        GgArena arena = gg_arena_init(GG_BUF(svcuid_mem));
        GgError ret
            = gg_base64_encode(GG_BUF(svcuid->val), &arena, &svcuid_str);
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to encode SVCUID.");
            return GG_ERR_FATAL;
        }
    }

    GgError ret = eventstream_encode(
        &resp_buffer,
        (EventStreamHeader[]) {
            { GG_STR(":message-type"),
              { EVENTSTREAM_INT32, .int32 = EVENTSTREAM_CONNECT_ACK } },
            { GG_STR(":message-flags"),
              { EVENTSTREAM_INT32, .int32 = EVENTSTREAM_CONNECTION_ACCEPTED } },
            { GG_STR(":stream-id"), { EVENTSTREAM_INT32, .int32 = 0 } },
            { GG_STR("svcuid"), { EVENTSTREAM_STRING, .string = svcuid_str } },
        },
        (svcuid != NULL) ? 4 : 3,
        GG_NULL_READER
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    return ggl_socket_handle_write(&pool, handle, resp_buffer);
}

static GgError handle_conn_init(
    uint32_t handle,
    EventStreamMessage *msg,
    EventStreamCommonHeaders common_headers,
    GgArena *alloc
) {
    GG_LOGD("Handling connect for %d.", handle);

    GgError ret = validate_conn_msg(msg, common_headers);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    GgMap payload_data = { 0 };
    ret = deserialize_payload(msg->payload, &payload_data, alloc);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Connect payload is not valid json.");
        return ret;
    }

    GgObject *auth_token_obj;
    GgObject *component_name_obj;
    ret = gg_map_validate(
        payload_data,
        GG_MAP_SCHEMA(
            { GG_STR("authToken"), GG_OPTIONAL, GG_TYPE_BUF, &auth_token_obj },
            { GG_STR("componentName"),
              GG_OPTIONAL,
              GG_TYPE_BUF,
              &component_name_obj },
        )
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Connect payload key has unexpected non-string value.");
        return GG_ERR_INVALID;
    }

    GglSvcuid svcuid;
    GglComponentHandle component_handle = 0;

    if (auth_token_obj != NULL) {
        GG_LOGD("Client %d provided authToken.", handle);

        ret = ggl_ipc_svcuid_from_str(
            gg_obj_into_buf(*auth_token_obj), &svcuid
        );
        if (ret == GG_ERR_OK) {
            ret = ggl_ipc_components_get_handle(svcuid, &component_handle);
        }
        if (ret != GG_ERR_OK) {
            GG_LOGE("Client %d failed authentication: invalid svcuid.", handle);
            return ret;
        }

        if (component_name_obj != NULL) {
            GG_LOGD("Client %d also provided componentName.", handle);

            GgBuffer component_name = gg_obj_into_buf(*component_name_obj);
            GgBuffer stored_name
                = ggl_ipc_components_get_name(component_handle);

            if (!gg_buffer_eq(component_name, stored_name)) {
                GG_LOGE(
                    "Client %d componentName (%.*s) does not match svcuid.",
                    handle,
                    (int) component_name.len,
                    component_name.data
                );
                return GG_ERR_FAILURE;
            }
        }
    } else if (component_name_obj != NULL) {
        GG_LOGD("Client %d provided componentName.", handle);

        GgBuffer component_name = gg_obj_into_buf(*component_name_obj);

        pid_t pid = 0;
        ret = ggl_socket_handle_get_peer_pid(&pool, handle, &pid);
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to get pid of client %d.", handle);
            return ret;
        }

        ret = ggl_ipc_auth_validate_name(pid, component_name);
        if (ret != GG_ERR_OK) {
            GG_LOGE(
                "Client %d failed to authenticate as %.*s.",
                handle,
                (int) component_name.len,
                component_name.data
            );
            return ret;
        }

        ret = ggl_ipc_components_register(
            component_name, &component_handle, &svcuid
        );
        if (ret != GG_ERR_OK) {
            return ret;
        }
    } else {
        GG_LOGE(
            "Client %d did not provide authToken or componentName.", handle
        );
        return GG_ERR_INVALID;
    }

    GG_LOGT("Setting %d as connected.", handle);

    ret = ggl_socket_handle_protected(
        set_conn_component, &component_handle, &pool, handle
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ret = send_conn_resp(handle, (auth_token_obj == NULL) ? &svcuid : NULL);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    GG_LOGD("Successful connection.");
    return GG_ERR_OK;
}

static GgError send_stream_error(
    uint32_t handle, int32_t stream_id, GglIpcError ipc_error
) {
    GG_LOGE("Sending error on client %u stream %d.", handle, stream_id);

    GG_MTX_SCOPE_GUARD(&resp_array_mtx);
    GgBuffer resp_buffer = GG_BUF(resp_array);

    GgBuffer service_model_type;
    GgBuffer error_code;

    ggl_ipc_err_info(ipc_error.error_code, &error_code, &service_model_type);

    EventStreamHeader resp_headers[] = {
        { GG_STR(":message-type"),
          { EVENTSTREAM_INT32, .int32 = EVENTSTREAM_APPLICATION_ERROR } },
        { GG_STR(":message-flags"),
          { EVENTSTREAM_INT32, .int32 = EVENTSTREAM_TERMINATE_STREAM } },
        { GG_STR(":stream-id"), { EVENTSTREAM_INT32, .int32 = stream_id } },
        { GG_STR(":content-type"),
          { EVENTSTREAM_STRING, .string = GG_STR("application/json") } },
        { GG_STR("service-model-type"),
          { EVENTSTREAM_STRING, .string = service_model_type } },
    };
    size_t resp_headers_len = sizeof(resp_headers) / sizeof(resp_headers[0]);

    GgObject payload = gg_obj_map(GG_MAP(
        gg_kv(GG_STR("_message"), gg_obj_buf(ipc_error.message)),
        gg_kv(GG_STR("_errorCode"), gg_obj_buf(error_code))
    ));
    GgError ret = eventstream_encode(
        &resp_buffer, resp_headers, resp_headers_len, gg_json_reader(&payload)
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    return ggl_socket_handle_write(&pool, handle, resp_buffer);
}

static GgError handle_stream_operation(
    uint32_t handle,
    EventStreamMessage *msg,
    EventStreamCommonHeaders common_headers,
    GglIpcError *ipc_error,
    GgArena *alloc
) {
    if (common_headers.message_type != EVENTSTREAM_APPLICATION_MESSAGE) {
        GG_LOGE("Client sent unhandled message type.");
        return GG_ERR_INVALID;
    }
    if ((common_headers.message_flags & EVENTSTREAM_FLAGS_MASK) != 0) {
        GG_LOGE("Client request has flags set.");
        return GG_ERR_INVALID;
    }

    GgBuffer operation = { 0 };

    {
        bool operation_set = false;
        EventStreamHeaderIter iter = msg->headers;
        EventStreamHeader header;

        while (eventstream_header_next(&iter, &header) == GG_ERR_OK) {
            if (gg_buffer_eq(header.name, GG_STR("operation"))) {
                if (header.value.type != EVENTSTREAM_STRING) {
                    GG_LOGE("operation header not string.");
                    return GG_ERR_INVALID;
                }
                operation = header.value.string;
                operation_set = true;
            }
        }

        if (!operation_set) {
            GG_LOGE("Client request missing operation header.");
            return GG_ERR_INVALID;
        }
    }

    GgMap payload_data = { 0 };
    GgError ret = deserialize_payload(msg->payload, &payload_data, alloc);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    return ggl_ipc_handle_operation(
        operation, payload_data, handle, common_headers.stream_id, ipc_error
    );
}

static GgError handle_operation(
    uint32_t handle,
    EventStreamMessage *msg,
    EventStreamCommonHeaders common_headers,
    GgArena *alloc
) {
    if (common_headers.stream_id == 0) {
        GG_LOGE("Application message has zero :stream-id.");
        return GG_ERR_INVALID;
    }

    if ((common_headers.message_flags & EVENTSTREAM_TERMINATE_STREAM) != 0) {
        GG_LOGD(
            "Termination requested of stream %d for %d.",
            common_headers.stream_id,
            handle
        );
        ggl_ipc_terminate_stream(handle, common_headers.stream_id);
        return GG_ERR_OK;
    }

    GG_LOGD(
        "Handling operation on stream %d for %d.",
        common_headers.stream_id,
        handle
    );

    GglIpcError ipc_error = GGL_IPC_ERROR_DEFAULT;

    GgError ret = handle_stream_operation(
        handle, msg, common_headers, &ipc_error, alloc
    );
    if (ret == GG_ERR_FATAL) {
        return GG_ERR_FAILURE;
    }

    if (ret != GG_ERR_OK) {
        return send_stream_error(handle, common_headers.stream_id, ipc_error);
    }

    return GG_ERR_OK;
}

static void get_conn_component(void *ctx, size_t index) {
    GglComponentHandle *handle = ctx;
    *handle = client_components[index];
}

GgError ggl_ipc_get_component_name(uint32_t handle, GgBuffer *component_name) {
    GglComponentHandle component_handle = { 0 };
    GgError ret = ggl_socket_handle_protected(
        get_conn_component, &component_handle, &pool, handle
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    *component_name = ggl_ipc_components_get_name(component_handle);
    return GG_ERR_OK;
}

static GgError client_ready(void *ctx, uint32_t handle) {
    (void) ctx;

    static uint8_t payload_array[GGL_IPC_MAX_MSG_LEN];
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
        return ret;
    }

    if (prelude.data_len > recv_buffer.len) {
        GG_LOGE("EventStream packet does not fit in configured IPC buffer size."
        );
        return GG_ERR_NOMEM;
    }

    GgBuffer data_section = gg_buffer_substr(recv_buffer, 0, prelude.data_len);

    ret = ggl_socket_handle_read(&pool, handle, data_section);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    EventStreamMessage msg;

    ret = eventstream_decode(&prelude, data_section, &msg);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    EventStreamCommonHeaders common_headers;
    ret = eventstream_get_common_headers(&msg, &common_headers);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    GG_LOGT("Retrieving connection state for %d.", handle);
    GglComponentHandle component_handle = 0;
    ret = ggl_socket_handle_protected(
        get_conn_component, &component_handle, &pool, handle
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    GgArena payload_decode_alloc = gg_arena_init(
        GG_BUF((uint8_t[sizeof(GgObject[GG_MAX_OBJECT_SUBOBJECTS])]) { 0 })
    );

    if (component_handle == 0) {
        return handle_conn_init(
            handle, &msg, common_headers, &payload_decode_alloc
        );
    }

    return handle_operation(
        handle, &msg, common_headers, &payload_decode_alloc
    );
}

GgError ggl_ipc_listen(const GgBuffer *socket_name, GgBuffer socket_path) {
    return ggl_socket_server_listen(
        socket_name, socket_path, 0666, &pool, client_ready, NULL
    );
}

GgError ggl_ipc_response_send(
    uint32_t handle,
    int32_t stream_id,
    GgBuffer service_model_type,
    GgMap response
) {
    GG_LOGD("Responding to operation on stream %d for %d.", stream_id, handle);

    GG_MTX_SCOPE_GUARD(&resp_array_mtx);
    GgBuffer resp_buffer = GG_BUF(resp_array);

    EventStreamHeader resp_headers[] = {
        { GG_STR(":message-type"),
          { EVENTSTREAM_INT32, .int32 = EVENTSTREAM_APPLICATION_MESSAGE } },
        { GG_STR(":message-flags"), { EVENTSTREAM_INT32, .int32 = 0 } },
        { GG_STR(":stream-id"), { EVENTSTREAM_INT32, .int32 = stream_id } },

        { GG_STR(":content-type"),
          { EVENTSTREAM_STRING, .string = GG_STR("application/json") } },
        { GG_STR("service-model-type"),
          { EVENTSTREAM_STRING, .string = service_model_type } },
    };
    size_t resp_headers_len = sizeof(resp_headers) / sizeof(resp_headers[0]);

    if (service_model_type.len == 0) {
        resp_headers_len -= 1;
    }

    GgObject resp_obj = gg_obj_map(response);
    GgError ret = eventstream_encode(
        &resp_buffer, resp_headers, resp_headers_len, gg_json_reader(&resp_obj)
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    return ggl_socket_handle_write(&pool, handle, resp_buffer);
}
