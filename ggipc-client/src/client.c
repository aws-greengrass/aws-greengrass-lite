// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggipc/client.h"
#include <sys/types.h>
#include <assert.h>
#include <ggl/base64.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/cleanup.h>
#include <ggl/constants.h>
#include <ggl/error.h>
#include <ggl/eventstream/decode.h>
#include <ggl/eventstream/encode.h>
#include <ggl/eventstream/rpc.h>
#include <ggl/eventstream/types.h>
#include <ggl/file.h>
#include <ggl/io.h>
#include <ggl/json_decode.h>
#include <ggl/json_encode.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/socket.h>
#include <ggl/vector.h>
#include <limits.h>
#include <pthread.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

/// Maximum size of eventstream packet.
/// Can be configured with `-DGGL_IPC_MAX_MSG_LEN=<N>`.
#ifndef GGL_IPC_MAX_MSG_LEN
#define GGL_IPC_MAX_MSG_LEN 10000
#endif

static uint8_t payload_array[GGL_IPC_MAX_MSG_LEN];
static pthread_mutex_t payload_array_mtx = PTHREAD_MUTEX_INITIALIZER;

static GglError send_message(
    int conn,
    const EventStreamHeader *headers,
    size_t headers_len,
    GglMap *payload
) {
    GGL_MTX_SCOPE_GUARD(&payload_array_mtx);

    GglBuffer send_buffer = GGL_BUF(payload_array);

    GglReader reader = payload != NULL ? ggl_json_reader(&GGL_OBJ_MAP(*payload))
                                       : GGL_NULL_READER;
    GglError ret
        = eventstream_encode(&send_buffer, headers, headers_len, reader);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return ggl_socket_write(conn, send_buffer);
}

static GglError get_message(
    int conn,
    GglBuffer recv_buffer,
    EventStreamMessage *msg,
    EventStreamCommonHeaders *common_headers,
    GglAlloc *alloc,
    GglObject *payload
) {
    assert(msg != NULL);

    GglBuffer prelude_buf = ggl_buffer_substr(recv_buffer, 0, 12);
    assert(prelude_buf.len == 12);

    GglError ret = ggl_socket_read(conn, prelude_buf);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    EventStreamPrelude prelude;
    ret = eventstream_decode_prelude(prelude_buf, &prelude);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (prelude.data_len > recv_buffer.len) {
        GGL_LOGE("EventStream packet does not fit in IPC packet buffer size.");
        return GGL_ERR_NOMEM;
    }

    GglBuffer data_section
        = ggl_buffer_substr(recv_buffer, 0, prelude.data_len);

    ret = ggl_socket_read(conn, data_section);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = eventstream_decode(&prelude, data_section, msg);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (common_headers != NULL) {
        ret = eventstream_get_common_headers(msg, common_headers);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    if (payload != NULL) {
        ret = ggl_json_decode_destructive(msg->payload, alloc, payload);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        msg->payload.len = 0;
    }

    return GGL_ERR_OK;
}

GglError ggipc_connect_auth(GglBuffer socket_path, GglBuffer *svcuid, int *fd) {
    int conn = -1;
    GglError ret = ggl_connect(socket_path, &conn);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    GGL_CLEANUP_ID(conn_cleanup, cleanup_close, conn);

    EventStreamHeader headers[] = {
        { GGL_STR(":message-type"),
          { EVENTSTREAM_INT32, .int32 = EVENTSTREAM_CONNECT } },
        { GGL_STR(":message-flags"), { EVENTSTREAM_INT32, .int32 = 0 } },
        { GGL_STR(":stream-id"), { EVENTSTREAM_INT32, .int32 = 0 } },
        { GGL_STR("authenticate"), { EVENTSTREAM_INT32, .int32 = 1 } },
        { GGL_STR(":version"),
          { EVENTSTREAM_STRING, .string = GGL_STR("0.1.0") } },
    };
    size_t headers_len = sizeof(headers) / sizeof(headers[0]);

    ret = send_message(conn, headers, headers_len, NULL);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GGL_MTX_SCOPE_GUARD(&payload_array_mtx);

    GglBuffer recv_buffer = GGL_BUF(payload_array);
    EventStreamMessage msg = { 0 };
    EventStreamCommonHeaders common_headers;

    ret = get_message(conn, recv_buffer, &msg, &common_headers, NULL, NULL);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (common_headers.message_type != EVENTSTREAM_CONNECT_ACK) {
        GGL_LOGE("Connection response not an ack.");
        return GGL_ERR_FAILURE;
    }

    if ((common_headers.message_flags & EVENTSTREAM_CONNECTION_ACCEPTED) == 0) {
        GGL_LOGE("Connection response missing accepted flag.");
        return GGL_ERR_FAILURE;
    }

    EventStreamHeaderIter iter = msg.headers;
    EventStreamHeader header;

    while (eventstream_header_next(&iter, &header) == GGL_ERR_OK) {
        if (ggl_buffer_eq(header.name, GGL_STR("svcuid"))) {
            if (header.value.type != EVENTSTREAM_STRING) {
                GGL_LOGE("Response svcuid header not string.");
                return GGL_ERR_INVALID;
            }

            if (svcuid != NULL) {
                if (svcuid->len < header.value.string.len) {
                    GGL_LOGE("Insufficient buffer space for svcuid.");
                    return GGL_ERR_NOMEM;
                }

                memcpy(
                    svcuid->data,
                    header.value.string.data,
                    header.value.string.len
                );
                svcuid->len = header.value.string.len;
            }

            if (fd != NULL) {
                // false positive
                // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
                conn_cleanup = -1;
                *fd = conn;
            }

            return GGL_ERR_OK;
        }
    }

    GGL_LOGE("Response missing svcuid header.");
    return GGL_ERR_FAILURE;
}

GglError ggipc_call(
    int conn,
    GglBuffer operation,
    GglMap params,
    GglAlloc *alloc,
    GglObject *result
) {
    EventStreamHeader headers[] = {
        { GGL_STR(":message-type"),
          { EVENTSTREAM_INT32, .int32 = EVENTSTREAM_APPLICATION_MESSAGE } },
        { GGL_STR(":message-flags"), { EVENTSTREAM_INT32, .int32 = 0 } },
        { GGL_STR(":stream-id"), { EVENTSTREAM_INT32, .int32 = 1 } },
        { GGL_STR("operation"), { EVENTSTREAM_STRING, .string = operation } },
    };
    size_t headers_len = sizeof(headers) / sizeof(headers[0]);

    GglError ret = send_message(conn, headers, headers_len, &params);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GGL_MTX_SCOPE_GUARD(&payload_array_mtx);

    GglBuffer recv_buffer = GGL_BUF(payload_array);
    EventStreamMessage msg = { 0 };
    EventStreamCommonHeaders common_headers;

    ret = get_message(conn, recv_buffer, &msg, &common_headers, alloc, result);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (result != NULL) {
        ret = ggl_obj_buffer_copy(result, alloc);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    if (common_headers.stream_id != 1) {
        GGL_LOGE("Unknown stream id received.");
        return GGL_ERR_FAILURE;
    }

    if (common_headers.message_type != EVENTSTREAM_APPLICATION_MESSAGE) {
        GGL_LOGE("Connection response not an ack.");
        return GGL_ERR_FAILURE;
    }

    return GGL_ERR_OK;
}

GglError ggipc_private_get_system_config(
    int conn, GglBuffer key, GglBuffer *value
) {
    GglBumpAlloc balloc = ggl_bump_alloc_init(*value);
    GglObject resp;
    GglError ret = ggipc_call(
        conn,
        GGL_STR("aws.greengrass.private#GetSystemConfig"),
        GGL_MAP({ GGL_STR("key"), GGL_OBJ_BUF(key) }),
        &balloc.alloc,
        &resp
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (resp.type != GGL_TYPE_BUF) {
        GGL_LOGE("Config value is not a string.");
        return GGL_ERR_FAILURE;
    }

    *value = resp.buf;

    GGL_LOGT(
        "Read %.*s: %.*s.",
        (int) key.len,
        key.data,
        (int) value->len,
        value->data
    );

    return GGL_ERR_OK;
}

GglError ggipc_get_config_str(
    int conn, GglBufList key_path, GglBuffer *component_name, GglBuffer *value
) {
    GglObjVec path_vec = GGL_OBJ_VEC((GglObject[GGL_MAX_OBJECT_DEPTH]) { 0 });
    GglError ret = GGL_ERR_OK;
    for (size_t i = 0; i < key_path.len; i++) {
        ggl_obj_vec_chain_push(&ret, &path_vec, GGL_OBJ_BUF(key_path.bufs[i]));
    }
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Key path too long.");
        return GGL_ERR_NOMEM;
    }

    GglKVVec args = GGL_KV_VEC((GglKV[2]) { 0 });
    (void) ggl_kv_vec_push(
        &args, (GglKV) { GGL_STR("keyPath"), GGL_OBJ_LIST(path_vec.list) }
    );
    if (component_name != NULL) {
        (void) ggl_kv_vec_push(
            &args,
            (GglKV) { GGL_STR("componentName"), GGL_OBJ_BUF(*component_name) }
        );
    }

    static uint8_t resp_mem[sizeof(GglKV) + sizeof("value") + PATH_MAX];
    GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(resp_mem));
    GglObject resp;
    ret = ggipc_call(
        conn,
        GGL_STR("aws.greengrass#GetConfiguration"),
        args.map,
        &balloc.alloc,
        &resp
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (resp.type != GGL_TYPE_MAP) {
        GGL_LOGE("Config value is not a map.");
        return GGL_ERR_FAILURE;
    }

    GglObject *resp_value;
    ret = ggl_map_validate(
        resp.map,
        GGL_MAP_SCHEMA({ GGL_STR("value"), true, GGL_TYPE_BUF, &resp_value })
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed validating server response.");
        return GGL_ERR_INVALID;
    }

    GglBumpAlloc ret_alloc = ggl_bump_alloc_init(*value);
    ret = ggl_obj_buffer_copy(resp_value, &ret_alloc.alloc);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Insufficent memory provided for response.");
        return ret;
    }

    *value = resp_value->buf;
    return GGL_ERR_OK;
}

// TODO: use GglByteVec for payload to allow in-place base64 encoding and remove
// alloc
GglError ggipc_publish_to_iot_core(
    int conn,
    GglBuffer topic_name,
    GglBuffer payload,
    uint8_t qos,
    GglAlloc *alloc
) {
    assert(qos <= 2);
    GGL_LOGT("Topic name len: %zu", topic_name.len);
    GglBuffer qos_buffer = GGL_BUF((uint8_t[1]) { qos + (uint8_t) '0' });
    GglBuffer encoded_payload;
    GglError ret = ggl_base64_encode(payload, alloc, &encoded_payload);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    GglMap args = GGL_MAP(
        { GGL_STR("topicName"), GGL_OBJ_BUF(topic_name) },
        { GGL_STR("payload"), GGL_OBJ_BUF(encoded_payload) },
        { GGL_STR("qos"), GGL_OBJ_BUF(qos_buffer) }
    );

    return ggipc_call(
        conn, GGL_STR("aws.greengrass#PublishToIoTCore"), args, NULL, NULL
    );
}
