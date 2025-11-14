// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "client_common.h"
#include "object_serde.h"
#include "types.h"
#include <assert.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/eventstream/decode.h>
#include <gg/eventstream/encode.h>
#include <gg/eventstream/types.h>
#include <gg/file.h> // IWYU pragma: keep (TODO: remove after file.h refactor)
#include <gg/io.h>
#include <gg/log.h>
#include <gg/object.h>
#include <gg/socket.h>
#include <gg/vector.h>
#include <ggl/core_bus/constants.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

uint8_t ggl_core_bus_client_payload_array[GGL_COREBUS_MAX_MSG_LEN];
pthread_mutex_t ggl_core_bus_client_payload_array_mtx
    = PTHREAD_MUTEX_INITIALIZER;

static GgError interface_connect(GgBuffer interface, int *conn_fd) {
    assert(conn_fd != NULL);

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

    return gg_connect(socket_path.buf, conn_fd);
}

GgError ggl_client_send_message(
    GgBuffer interface,
    GglCoreBusRequestType type,
    GgBuffer method,
    GgMap params,
    int *conn_fd
) {
    int conn = -1;
    GG_LOGT("Connecting to %.*s.", (int) interface.len, interface.data);
    GgError ret = interface_connect(interface, &conn);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    GG_CLEANUP_ID(conn_cleanup, cleanup_close, conn);

    GG_MTX_SCOPE_GUARD(&ggl_core_bus_client_payload_array_mtx);

    GgBuffer send_buffer = GG_BUF(ggl_core_bus_client_payload_array);

    EventStreamHeader headers[] = {
        { GG_STR("method"), { EVENTSTREAM_STRING, .string = method } },
        { GG_STR("type"), { EVENTSTREAM_INT32, .int32 = (int32_t) type } },
    };
    size_t headers_len = sizeof(headers) / sizeof(headers[0]);

    GgObject params_obj = gg_obj_map(params);
    ret = eventstream_encode(
        &send_buffer, headers, headers_len, ggl_serialize_reader(&params_obj)
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    GG_LOGT("Writing data to %.*s.", (int) interface.len, interface.data);

    ret = gg_socket_write(conn, send_buffer);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores) false positive
    conn_cleanup = -1;
    *conn_fd = conn;
    return GG_ERR_OK;
}

GgError ggl_client_get_response(
    GgReader reader,
    GgBuffer recv_buffer,
    GgError *error,
    EventStreamMessage *response
) {
    GgBuffer prelude_buf = gg_buffer_substr(recv_buffer, 0, 12);
    assert(prelude_buf.len == 12);

    GgError ret = gg_reader_call_exact(reader, prelude_buf);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    EventStreamPrelude prelude;
    ret = eventstream_decode_prelude(prelude_buf, &prelude);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    if (prelude.data_len > recv_buffer.len) {
        GG_LOGE("EventStream packet does not fit in core bus buffer size.");
        return GG_ERR_NOMEM;
    }

    GgBuffer data_section = gg_buffer_substr(recv_buffer, 0, prelude.data_len);

    ret = gg_reader_call_exact(reader, data_section);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ret = eventstream_decode(&prelude, data_section, response);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    EventStreamHeaderIter iter = response->headers;
    EventStreamHeader header;

    while (eventstream_header_next(&iter, &header) == GG_ERR_OK) {
        if (gg_buffer_eq(header.name, GG_STR("error"))) {
            GG_LOGW("Server responded with an error.");
            if (error != NULL) {
                *error = GG_ERR_FAILURE;
            }
            if (header.value.type != EVENTSTREAM_INT32) {
                GG_LOGE("Response error header not int.");
            } else {
                // TODO: Handle unknown error value
                if (error != NULL) {
                    *error = (GgError) header.value.int32;
                }
            }
            return GG_ERR_REMOTE;
        }
    }

    return GG_ERR_OK;
}
