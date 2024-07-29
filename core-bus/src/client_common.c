// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "client_common.h"
#include "object_serde.h"
#include "types.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <ggl/buffer.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/eventstream/decode.h>
#include <ggl/eventstream/encode.h>
#include <ggl/eventstream/types.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/socket.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

uint8_t ggl_core_bus_client_payload_array[GGL_COREBUS_MAX_MSG_LEN];
pthread_mutex_t ggl_core_bus_client_payload_array_mtx
    = PTHREAD_MUTEX_INITIALIZER;

static GglError interface_connect(GglBuffer interface, int *conn_fd) {
    assert(conn_fd != NULL);

    char socket_path
        [GGL_INTERFACE_SOCKET_PREFIX_LEN + GGL_INTERFACE_NAME_MAX_LEN + 1]
        = GGL_INTERFACE_SOCKET_PREFIX;

    if (interface.len > GGL_INTERFACE_NAME_MAX_LEN) {
        GGL_LOGE("core-bus-client", "Interface name too long.");
        return GGL_ERR_RANGE;
    }

    memcpy(
        &socket_path[GGL_INTERFACE_SOCKET_PREFIX_LEN],
        interface.data,
        interface.len
    );

    int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd == -1) {
        int err = errno;
        GGL_LOGE("core-bus-client", "Failed to create socket: %d.", err);
        return GGL_ERR_FATAL;
    }
    GGL_DEFER(close, sockfd);

    fcntl(sockfd, F_SETFD, FD_CLOEXEC);

    struct sockaddr_un addr = { .sun_family = AF_UNIX, .sun_path = { 0 } };

    size_t path_len = strlen(socket_path);

    if (path_len >= sizeof(addr.sun_path)) {
        GGL_LOGE("socket-client", "Socket path too long.");
        return GGL_ERR_FAILURE;
    }

    memcpy(addr.sun_path, socket_path, path_len);

    if (connect(sockfd, (const struct sockaddr *) &addr, sizeof(addr)) == -1) {
        int err = errno;
        GGL_LOGW("socket-client", "Failed to connect to server: %d.", err);
        return GGL_ERR_FAILURE;
    }

    // To prevent deadlocking on hanged server, add a timeout
    struct timeval timeout = { .tv_sec = 5 };
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    GGL_DEFER_CANCEL(sockfd);
    *conn_fd = sockfd;
    return GGL_ERR_OK;
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
    GGL_DEFER(close, conn);

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
        GGL_LOGE(
            "core-bus-client",
            "EventStream packet does not fit in core bus buffer size."
        );
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
            if (error != NULL) {
                *error = GGL_ERR_FAILURE;
            }
            if (header.value.type != EVENTSTREAM_INT32) {
                GGL_LOGE("core-bus-client", "Response error header not int.");
            } else {
                // TODO: Handle unknown error value
                if (error != NULL) {
                    *error = (GglError) header.value.int32;
                }
            }
            return GGL_ERR_FAILURE;
        }
    }

    return GGL_ERR_OK;
}
