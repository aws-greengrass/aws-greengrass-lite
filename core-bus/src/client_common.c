// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "client_common.h"
#include "ggl/core_bus/constants.h"
#include "object_serde.h"
#include "types.h"
#include <assert.h>
#include <ggl/buffer.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/eventstream/decode.h>
#include <ggl/eventstream/encode.h>
#include <ggl/eventstream/types.h>
#include <ggl/file.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/socket.h>
#include <ggl/vector.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

uint8_t ggl_core_bus_client_payload_array[GGL_COREBUS_MAX_MSG_LEN];
pthread_mutex_t ggl_core_bus_client_payload_array_mtx
    = PTHREAD_MUTEX_INITIALIZER;

static GglError interface_connect(GglBuffer interface, int *conn_fd) {
    assert(conn_fd != NULL);

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

    return ggl_connect(socket_path.buf, conn_fd);
}

static GglError payload_writer(GglBuffer *buf, void *payload) {
    assert(buf != NULL);
    assert(payload != NULL);

    GglMap *map = payload;
    return ggl_serialize(GGL_OBJ(*map), buf);
}

GglError ggl_client_send_message(
    GglBuffer interface,
    GglCoreBusRequestType type,
    GglBuffer method,
    GglMap params,
    int *conn_fd
) {
    int conn = -1;
    GglError ret = interface_connect(interface, &conn);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    GGL_DEFER(ggl_close, conn);

    pthread_mutex_lock(&ggl_core_bus_client_payload_array_mtx);
    GGL_DEFER(pthread_mutex_unlock, ggl_core_bus_client_payload_array_mtx);

    GglBuffer send_buffer = GGL_BUF(ggl_core_bus_client_payload_array);

    EventStreamHeader headers[] = {
        { GGL_STR("method"), { EVENTSTREAM_STRING, .string = method } },
        { GGL_STR("type"), { EVENTSTREAM_INT32, .int32 = (int32_t) type } },
    };
    size_t headers_len = sizeof(headers) / sizeof(headers[0]);

    ret = eventstream_encode(
        &send_buffer, headers, headers_len, payload_writer, &params
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = ggl_write_exact(conn, send_buffer);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GGL_DEFER_CANCEL(conn);
    *conn_fd = conn;
    return GGL_ERR_OK;
}

GglError ggl_fd_reader(void *ctx, GglBuffer buf) {
    int *fd_ptr = ctx;
    return ggl_read_exact(*fd_ptr, buf);
}

GglError ggl_client_get_response(
    GglError (*reader)(void *ctx, GglBuffer buf),
    void *reader_ctx,
    GglBuffer recv_buffer,
    GglError *error,
    EventStreamMessage *response
) {
    GglBuffer prelude_buf = ggl_buffer_substr(recv_buffer, 0, 12);
    assert(prelude_buf.len == 12);

    GglError ret = reader(reader_ctx, prelude_buf);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    EventStreamPrelude prelude;
    ret = eventstream_decode_prelude(prelude_buf, &prelude);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (prelude.data_len > recv_buffer.len) {
        GGL_LOGE("EventStream packet does not fit in core bus buffer size.");
        return GGL_ERR_NOMEM;
    }

    GglBuffer data_section
        = ggl_buffer_substr(recv_buffer, 0, prelude.data_len);

    ret = reader(reader_ctx, data_section);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = eventstream_decode(&prelude, data_section, response);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    EventStreamHeaderIter iter = response->headers;
    EventStreamHeader header;

    while (eventstream_header_next(&iter, &header) == GGL_ERR_OK) {
        if (ggl_buffer_eq(header.name, GGL_STR("error"))) {
            GGL_LOGW("Server responded with an error.");
            if (error != NULL) {
                *error = GGL_ERR_FAILURE;
            }
            if (header.value.type != EVENTSTREAM_INT32) {
                GGL_LOGE("Response error header not int.");
            } else {
                // TODO: Handle unknown error value
                if (error != NULL) {
                    *error = (GglError) header.value.int32;
                }
            }
            return GGL_ERR_REMOTE;
        }
    }

    return GGL_ERR_OK;
}
