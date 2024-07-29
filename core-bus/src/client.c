/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ggl/core_bus/client.h"
#include "object_serde.h"
#include "sys/un.h"
#include "types.h"
#include <assert.h>
#include <errno.h>
#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/eventstream/decode.h>
#include <ggl/eventstream/encode.h>
#include <ggl/eventstream/types.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

static uint8_t payload_array[GGL_COREBUS_MAX_MSG_LEN];
static pthread_mutex_t payload_array_mtx = PTHREAD_MUTEX_INITIALIZER;

static GglError socket_read(int fd, GglBuffer buf) {
    size_t read = 0;

    while (read < buf.len) {
        ssize_t ret = recv(fd, &buf.data[read], buf.len - read, MSG_WAITALL);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            int err = errno;
            GGL_LOGE("core-bus-server", "Failed to recv from client: %d.", err);
            return GGL_ERR_FAILURE;
        }
        if (ret == 0) {
            GGL_LOGD("core-bus-server", "Client socket closed");
            return GGL_ERR_NOCONN;
        }
        read += (size_t) ret;
    }

    assert(read == buf.len);
    return GGL_ERR_OK;
}

static GglError socket_write(int fd, GglBuffer buf) {
    size_t written = 0;

    while (written < buf.len) {
        ssize_t ret = write(fd, &buf.data[written], buf.len - written);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            int err = errno;
            GGL_LOGE("core-bus-server", "Failed to write to client: %d.", err);
            return GGL_ERR_FAILURE;
        }
        written += (size_t) ret;
    }

    assert(written == buf.len);
    return GGL_ERR_OK;
}

static GglError interface_connect(GglBuffer interface, int *conn) {
    assert(conn != NULL);

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

    GGL_DEFER_CANCEL(sockfd);
    *conn = sockfd;
    return GGL_ERR_OK;
}

static GglError payload_writer(GglBuffer *buf, void *payload) {
    assert(buf != NULL);
    assert(payload != NULL);

    GglMap *map = payload;
    return ggl_serialize(GGL_OBJ(*map), buf);
}

GglError ggl_notify(GglBuffer interface, GglBuffer method, GglMap params) {
    int conn = -1;
    GglError ret = interface_connect(interface, &conn);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    GGL_DEFER(close, conn);

    pthread_mutex_lock(&payload_array_mtx);
    GGL_DEFER(pthread_mutex_unlock, payload_array_mtx);

    GglBuffer send_buffer = GGL_BUF(payload_array);

    EventStreamHeader headers[] = {
        { GGL_STR("method"), { EVENTSTREAM_STRING, .string = method } },
        { GGL_STR("type"),
          { EVENTSTREAM_INT32, .int32 = (int32_t) CORE_BUS_NOTIFY } },
    };
    size_t headers_len = sizeof(headers) / sizeof(headers[0]);

    ret = eventstream_encode(
        &send_buffer, headers, headers_len, payload_writer, &params
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = socket_write(conn, send_buffer);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return GGL_ERR_OK;
}

GglError ggl_call(
    GglBuffer interface,
    GglBuffer method,
    GglMap params,
    GglError *error,
    GglAlloc *alloc,
    GglObject *result
) {
    int conn = -1;
    GglError ret = interface_connect(interface, &conn);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    GGL_DEFER(close, conn);

    {
        pthread_mutex_lock(&payload_array_mtx);
        GGL_DEFER(pthread_mutex_unlock, payload_array_mtx);

        GglBuffer send_buffer = GGL_BUF(payload_array);

        EventStreamHeader headers[] = {
            { GGL_STR("method"), { EVENTSTREAM_STRING, .string = method } },
            { GGL_STR("type"),
              { EVENTSTREAM_INT32, .int32 = (int32_t) CORE_BUS_CALL } },
        };
        size_t headers_len = sizeof(headers) / sizeof(headers[0]);

        ret = eventstream_encode(
            &send_buffer, headers, headers_len, payload_writer, &params
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        ret = socket_write(conn, send_buffer);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    pthread_mutex_lock(&payload_array_mtx);
    GGL_DEFER(pthread_mutex_unlock, payload_array_mtx);

    GglBuffer recv_buffer = GGL_BUF(payload_array);
    GglBuffer prelude_buf = ggl_buffer_substr(recv_buffer, 0, 12);
    assert(prelude_buf.len == 12);

    ret = socket_read(conn, prelude_buf);
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
            "core-bus",
            "EventStream packet does not fit in core bus buffer size."
        );
        return GGL_ERR_NOMEM;
    }

    GglBuffer data_section
        = ggl_buffer_substr(recv_buffer, 0, prelude.data_len);

    ret = socket_read(conn, data_section);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    EventStreamMessage msg;

    ret = eventstream_decode(&prelude, data_section, &msg);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    {
        EventStreamHeaderIter iter = msg.headers;
        EventStreamHeader header;

        while (eventstream_header_next(&iter, &header) == GGL_ERR_OK) {
            if (ggl_buffer_eq(header.name, GGL_STR("error"))) {
                if (error != NULL) {
                    *error = GGL_ERR_FAILURE;
                }
                if (header.value.type != EVENTSTREAM_INT32) {
                    GGL_LOGE(
                        "core-bus-client", "Response error header not int."
                    );
                } else {
                    // TODO: Handle unknown error value
                    if (error != NULL) {
                        *error = (GglError) header.value.int32;
                    }
                }
                return GGL_ERR_FAILURE;
            }
        }
    }

    ret = ggl_deserialize(alloc, true, msg.payload, result);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("core-bus-client", "Failed to decode response payload.");
        return ret;
    }

    return GGL_ERR_OK;
}
