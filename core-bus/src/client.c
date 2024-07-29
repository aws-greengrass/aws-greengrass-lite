/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ggl/core_bus/client.h"
#include "fcntl.h"
#include "object_serde.h"
#include "sys/un.h"
#include "types.h"
#include <assert.h>
#include <errno.h>
#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/eventstream/decode.h>
#include <ggl/eventstream/encode.h>
#include <ggl/eventstream/types.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/utils.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

// TODO: Split into separate .o files so linker can skip sub .o if sub not used

/** Maximum number of core-bus connections.
 * Can be configured with `-DGGL_IPC_MAX_CLIENTS=<N>`. */
#ifndef GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS
#define GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS 50
#endif

/** Maximum size of core-bus packet.
 * Can be configured with `-DGGL_IPC_MAX_MSG_LEN=<N>`. */
#ifndef GGL_COREBUS_MAX_MSG_LEN
#define GGL_COREBUS_MAX_MSG_LEN 10000
#endif

static_assert(
    GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS < UINT16_MAX,
    "Max subscriptions cannot exceed UINT16_MAX."
);

typedef struct {
    GglSubscribeCallback on_response;
    GglSubscribeCloseCallback on_close;
    void *ctx;
} SubCallbacks;

SubCallbacks sub_callbacks[GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS];
bool sub_claimed[GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS] = { 0 };
int sub_fd[GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS];
uint16_t generations[GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS];
static pthread_mutex_t sub_state_mtx = PTHREAD_MUTEX_INITIALIZER;

static uint8_t payload_array[GGL_COREBUS_MAX_MSG_LEN];
static pthread_mutex_t payload_array_mtx = PTHREAD_MUTEX_INITIALIZER;

static uint8_t sub_thread_payload_array[GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS]
                                       [GGL_COREBUS_MAX_MSG_LEN];

static bool get_sub_handle(uint32_t *handle, SubCallbacks callbacks, int fd) {
    pthread_mutex_lock(&sub_state_mtx);
    GGL_DEFER(pthread_mutex_unlock, sub_state_mtx);

    for (uint16_t i = 0; i < GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS; i++) {
        if (!sub_claimed[i]) {
            sub_claimed[i] = true;
            sub_callbacks[i] = callbacks;
            sub_fd[i] = fd;
            *handle = (uint32_t) generations[i] << 16 | i;
            return true;
        }
    }

    return false;
}

static bool release_sub_handle(uint32_t handle) {
    uint16_t index = (uint16_t) (handle & UINT16_MAX);
    uint16_t generation = (uint16_t) (handle >> 16);

    pthread_mutex_lock(&sub_state_mtx);
    GGL_DEFER(pthread_mutex_unlock, sub_state_mtx);

    if (generation != generations[index]) {
        GGL_LOGD("socket-client", "Generation mismatch in %s.", __func__);
        return false;
    }

    generations[index] += 1;
    sub_claimed[index] = false;

    return true;
}

GGL_DEFINE_DEFER(
    release_sub_handle, uint32_t, handle, release_sub_handle(*handle)
)

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

static GglError socket_read_protected(uint32_t handle, GglBuffer buf) {
    size_t read = 0;

    while (read < buf.len) {
        uint16_t index = (uint16_t) (handle & UINT16_MAX);
        uint16_t generation = (uint16_t) (handle >> 16);

        pthread_mutex_lock(&sub_state_mtx);
        GGL_DEFER(pthread_mutex_unlock, sub_state_mtx);

        if (generation != generations[index]) {
            GGL_LOGD("socket-client", "Generation mismatch in %s.", __func__);
            return GGL_ERR_NOCONN;
        }

        ssize_t ret
            = recv(sub_fd[index], &buf.data[read], buf.len - read, MSG_WAITALL);

        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                GGL_DEFER_FORCE(sub_state_mtx);
                ggl_sleep(1);
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

GGL_DEFINE_DEFER(
    ggl_client_sub_close, uint32_t, handle, ggl_client_sub_close(*handle)
)

static void *subscription_thread(void *args) {
    uint32_t *thread_handle = args;
    uint32_t handle = *thread_handle;
    free(thread_handle);
    GGL_DEFER(ggl_client_sub_close, handle);

    signal(SIGPIPE, SIG_IGN);

    uint16_t index = (uint16_t) (handle & UINT16_MAX);
    uint16_t generation = (uint16_t) (handle >> 16);
    uint8_t(*thread_payload_array)[GGL_COREBUS_MAX_MSG_LEN] = NULL;

    {
        pthread_mutex_lock(&sub_state_mtx);
        GGL_DEFER(pthread_mutex_unlock, sub_state_mtx);

        if (generation != generations[index]) {
            GGL_LOGD("socket-client", "Generation mismatch in %s.", __func__);
            return NULL;
        }

        fcntl(
            sub_fd[index],
            F_SETFL,
            fcntl(sub_fd[index], F_GETFL, 0) | O_NONBLOCK
        );
        thread_payload_array = &sub_thread_payload_array[index];
    }

    while (true) {
        GglBuffer recv_buffer = GGL_BUF(*thread_payload_array);
        GglBuffer prelude_buf = ggl_buffer_substr(recv_buffer, 0, 12);
        assert(prelude_buf.len == 12);

        GglError ret = socket_read_protected(handle, prelude_buf);
        if (ret != GGL_ERR_OK) {
            return NULL;
        }

        EventStreamPrelude prelude;
        ret = eventstream_decode_prelude(prelude_buf, &prelude);
        if (ret != GGL_ERR_OK) {
            return NULL;
        }

        if (prelude.data_len > recv_buffer.len) {
            GGL_LOGE(
                "core-bus",
                "EventStream packet does not fit in core bus buffer size."
            );
            return NULL;
        }

        GglBuffer data_section
            = ggl_buffer_substr(recv_buffer, 0, prelude.data_len);

        ret = socket_read_protected(handle, data_section);
        if (ret != GGL_ERR_OK) {
            return NULL;
        }

        {
            pthread_mutex_lock(&sub_state_mtx);
            GGL_DEFER(pthread_mutex_unlock, sub_state_mtx);

            if (generation != generations[index]) {
                GGL_LOGD(
                    "socket-client", "Generation mismatch in %s.", __func__
                );
                return NULL;
            }

            EventStreamMessage msg;

            ret = eventstream_decode(&prelude, data_section, &msg);
            if (ret != GGL_ERR_OK) {
                return NULL;
            }

            pthread_mutex_lock(&payload_array_mtx);
            GGL_DEFER(pthread_mutex_unlock, payload_array_mtx);

            GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(payload_array));

            GglObject result = GGL_OBJ_NULL();
            ret = ggl_deserialize(&balloc.alloc, false, msg.payload, &result);
            if (ret != GGL_ERR_OK) {
                GGL_LOGE(
                    "core-bus-client", "Failed to decode response payload."
                );
                return NULL;
            }

            if (sub_callbacks[index].on_response != NULL) {
                sub_callbacks[index].on_response(
                    sub_callbacks[index].ctx, handle, result
                );
            }
        }
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
GglError ggl_subscribe(
    GglBuffer interface,
    GglBuffer method,
    GglMap params,
    GglSubscribeCallback on_response,
    GglSubscribeCloseCallback on_close,
    void *ctx,
    GglError *error,
    uint32_t *handle
) {
    int conn = -1;
    GglError ret = interface_connect(interface, &conn);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    GGL_DEFER(close, conn);
    fcntl(conn, F_SETFD, FD_CLOEXEC);

    uint32_t sub_handle;
    bool bool_ret = get_sub_handle(
        &sub_handle,
        (SubCallbacks) {
            .on_response = on_response,
            .on_close = on_close,
            .ctx = ctx,
        },
        conn
    );
    if (!bool_ret) {
        GGL_LOGE("core-bus-client", "Exceeded max subscriptions.");
        return GGL_ERR_NOMEM;
    }
    GGL_DEFER(release_sub_handle, sub_handle);

    {
        pthread_mutex_lock(&payload_array_mtx);
        GGL_DEFER(pthread_mutex_unlock, payload_array_mtx);

        GglBuffer send_buffer = GGL_BUF(payload_array);

        EventStreamHeader headers[] = {
            { GGL_STR("method"), { EVENTSTREAM_STRING, .string = method } },
            { GGL_STR("type"),
              { EVENTSTREAM_INT32, .int32 = (int32_t) CORE_BUS_SUBSCRIBE } },
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

    bool accepted = false;

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
            if (ggl_buffer_eq(header.name, GGL_STR("accepted"))) {
                if ((header.value.type == EVENTSTREAM_INT32)
                    && (header.value.int32 == 1)) {
                    accepted = true;
                }
            }
        }
    }

    if (!accepted) {
        GGL_LOGE(
            "core-bus-client",
            "Response for subscription not accepted or error."
        );
        return GGL_ERR_FAILURE;
    }

    pthread_t sub_thread = { 0 };
    uint32_t *thread_handle = malloc(sizeof(uint32_t));
    if (thread_handle == NULL) {
        return GGL_ERR_NOMEM;
    }
    int sys_ret
        = pthread_create(&sub_thread, NULL, subscription_thread, thread_handle);
    if (sys_ret != 0) {
        free(thread_handle);
        GGL_LOGE("core-bus-client", "Failed to create subscription thread.");
        return GGL_ERR_FAILURE;
    }
    pthread_detach(sub_thread);

    GGL_DEFER_CANCEL(sub_handle);
    GGL_DEFER_CANCEL(conn);

    if (handle != NULL) {
        *handle = sub_handle;
    }

    return GGL_ERR_OK;
}

void ggl_client_sub_close(uint32_t handle) {
    uint16_t index = (uint16_t) (handle & UINT16_MAX);
    uint16_t generation = (uint16_t) (handle >> 16);

    pthread_mutex_lock(&sub_state_mtx);
    GGL_DEFER(pthread_mutex_unlock, sub_state_mtx);

    if (generation != generations[index]) {
        GGL_LOGD("socket-client", "Generation mismatch in %s.", __func__);
        return;
    }

    if (sub_callbacks[index].on_close != NULL) {
        sub_callbacks[index].on_close(sub_callbacks[index].ctx, handle);
    }

    close(sub_fd[index]);

    generations[index] += 1;
    sub_claimed[index] = false;
}
