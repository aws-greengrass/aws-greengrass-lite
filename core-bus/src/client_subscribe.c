/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "fcntl.h"
#include "ggl/core_bus/client.h"
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
#include <ggl/socket.h>
#include <ggl/socket_epoll.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

// This must be separate C file from rest of core bus functionality as it
// creates a thread on startup using a constructor function.
// When including a .a, the linker only uses .o files that resolve a needed
// symbol. Since this is a separate .o, that means it will only be included if
// ggl_subscribe is used by the binary, and the thread is only created in
// binaries using ggl_subscribe functionality.

/** Maximum number of core-bus connections.
 * Can be configured with `-DGGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS=<N>`. */
#ifndef GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS
#define GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS 50
#endif

#define PAYLOAD_MAX_SUBOBJECTS 50

static_assert(
    GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS < UINT16_MAX,
    "Max subscriptions cannot exceed UINT16_MAX."
);

typedef struct {
    GglSubscribeCallback on_response;
    GglSubscribeCloseCallback on_close;
    void *ctx;
} SubCallbacks;

static uint8_t payload_array[GGL_COREBUS_MAX_MSG_LEN];
static pthread_mutex_t payload_array_mtx = PTHREAD_MUTEX_INITIALIZER;

static SubCallbacks sub_callbacks[GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS];

static GglError reset_sub_state(uint32_t handle, size_t index);

static int sub_fds[GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS];
static uint16_t sub_generations[GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS];

GglSocketPool pool = {
    .max_fds = GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS,
    .fds = sub_fds,
    .generations = sub_generations,
    .on_register = reset_sub_state,
};

__attribute__((constructor)) static void init_sub_pool(void) {
    ggl_socket_pool_init(&pool);
}

static void *subscription_thread(void *args);

static int epoll_fd = -1;

/** Initializes subscription epoll and starts epoll thread.
 * Runs at startup (before main). */
__attribute__((constructor)) static void start_subscription_thread(void) {
    GglError ret = ggl_socket_epoll_create(&epoll_fd);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "core-bus-client",
            "Failed to create epoll for subscription responses."
        );
        // exit() is not re-entrant and this is safe as long as no spawned
        // thread can call exit()
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        exit(-1);
    }

    pthread_t sub_thread = { 0 };
    int sys_ret = pthread_create(&sub_thread, NULL, subscription_thread, NULL);
    if (sys_ret != 0) {
        GGL_LOGE(
            "core-bus-client", "Failed to create subscription response thread."
        );
        // exit() is not re-entrant and this is safe as long as no spawned
        // thread can call exit()
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        exit(-1);
    }
    pthread_detach(sub_thread);
}

static GglError reset_sub_state(uint32_t handle, size_t index) {
    (void) handle;
    sub_callbacks[index] = (SubCallbacks) { 0 };
    return GGL_ERR_OK;
}

static void set_sub_callbacks(void *ctx, size_t index) {
    SubCallbacks *callbacks = ctx;
    sub_callbacks[index] = *callbacks;
}

static void get_sub_callbacks(void *ctx, size_t index) {
    SubCallbacks *callbacks = ctx;
    *callbacks = sub_callbacks[index];
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
    fcntl(sockfd, F_SETFD, FD_CLOEXEC);
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

static GglError write_exact(int fd, GglBuffer buf) {
    // If SIGPIPE is not blocked, writing to a socket that the client has closed
    // will result in this process being killed.
    signal(SIGPIPE, SIG_IGN);

    size_t written = 0;

    while (written < buf.len) {
        ssize_t ret = write(fd, &buf.data[written], buf.len - written);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            int err = errno;
            GGL_LOGE("core-bus-client", "Failed to write to server: %d.", err);
            return GGL_ERR_FAILURE;
        }
        written += (size_t) ret;
    }

    assert(written == buf.len);
    return GGL_ERR_OK;
}

static GglError read_exact(int fd, GglBuffer buf) {
    size_t read = 0;

    while (read < buf.len) {
        ssize_t ret = recv(fd, &buf.data[read], buf.len - read, MSG_WAITALL);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            int err = errno;
            GGL_LOGE("core-bus-client", "Failed to recv from server: %d.", err);
            return GGL_ERR_FAILURE;
        }
        if (ret == 0) {
            GGL_LOGD("core-bus-client", "Socket closed by server.");
            return GGL_ERR_NOCONN;
        }
        read += (size_t) ret;
    }

    assert(read == buf.len);
    return GGL_ERR_OK;
}

static GglError payload_writer(GglBuffer *buf, void *payload) {
    assert(buf != NULL);
    assert(payload != NULL);

    GglMap *map = payload;
    return ggl_serialize(GGL_OBJ(*map), buf);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static GglError make_subscribe_request(
    int conn, GglBuffer method, GglMap params, GglError *server_error
) {
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

        GglError ret = eventstream_encode(
            &send_buffer, headers, headers_len, payload_writer, &params
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        ret = write_exact(conn, send_buffer);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    pthread_mutex_lock(&payload_array_mtx);
    GGL_DEFER(pthread_mutex_unlock, payload_array_mtx);

    GglBuffer recv_buffer = GGL_BUF(payload_array);
    GglBuffer prelude_buf = ggl_buffer_substr(recv_buffer, 0, 12);
    assert(prelude_buf.len == 12);

    GglError ret = read_exact(conn, prelude_buf);
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

    ret = read_exact(conn, data_section);
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
                if (server_error != NULL) {
                    *server_error = GGL_ERR_FAILURE;
                }
                if (header.value.type != EVENTSTREAM_INT32) {
                    GGL_LOGE(
                        "core-bus-client", "Response error header not int."
                    );
                } else {
                    // TODO: Handle unknown error value
                    if (server_error != NULL) {
                        *server_error = (GglError) header.value.int32;
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

    return GGL_ERR_OK;
}

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
    if (epoll_fd < 0) {
        GGL_LOGE("core-bus-client", "Subscription epoll not initialized.");
        return GGL_ERR_FATAL;
    }

    int conn = -1;
    GglError ret = interface_connect(interface, &conn);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    uint32_t sub_handle = 0;
    ret = ggl_socket_pool_register(&pool, conn, &sub_handle);
    if (ret != GGL_ERR_OK) {
        close(conn);
        return ret;
    }

    ret = make_subscribe_request(conn, method, params, error);
    if (ret != GGL_ERR_OK) {
        ggl_socket_close(&pool, sub_handle);
        return ret;
    }

    ggl_socket_with_index(
        set_sub_callbacks,
        &(SubCallbacks) {
            .on_response = on_response,
            .on_close = on_close,
            .ctx = ctx,
        },
        &pool,
        sub_handle
    );

    ret = ggl_socket_epoll_add(epoll_fd, conn, sub_handle);
    if (ret != GGL_ERR_OK) {
        ggl_socket_close(&pool, sub_handle);
        return ret;
    }

    if (handle != NULL) {
        *handle = sub_handle;
    }

    return GGL_ERR_OK;
}

void ggl_client_sub_close(uint32_t handle) {
    ggl_socket_close(&pool, handle);
}

static GglError get_subscription_response(uint32_t handle) {
    // Need separate data array as sub resp callback may call core bus APIs
    static uint8_t sub_resp_payload_array[GGL_COREBUS_MAX_MSG_LEN];
    static pthread_mutex_t sub_resp_payload_array_mtx
        = PTHREAD_MUTEX_INITIALIZER;

    pthread_mutex_lock(&sub_resp_payload_array_mtx);
    GGL_DEFER(pthread_mutex_unlock, sub_resp_payload_array_mtx);

    GglBuffer recv_buffer = GGL_BUF(sub_resp_payload_array);
    GglBuffer prelude_buf = ggl_buffer_substr(recv_buffer, 0, 12);
    assert(prelude_buf.len == 12);

    GglError ret = ggl_socket_read(&pool, handle, prelude_buf);
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
        return ret;
    }

    GglBuffer data_section
        = ggl_buffer_substr(recv_buffer, 0, prelude.data_len);

    ret = ggl_socket_read(&pool, handle, data_section);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    EventStreamMessage msg;

    ret = eventstream_decode(&prelude, data_section, &msg);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    static uint8_t obj_decode_mem[PAYLOAD_MAX_SUBOBJECTS * sizeof(GglObject)];
    GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(obj_decode_mem));

    GglObject result = GGL_OBJ_NULL();
    ret = ggl_deserialize(&balloc.alloc, false, msg.payload, &result);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "core-bus-client", "Failed to decode subscription response payload."
        );
        return ret;
    }

    SubCallbacks callbacks = { 0 };
    ret = ggl_socket_with_index(get_sub_callbacks, &callbacks, &pool, handle);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (callbacks.on_response != NULL) {
        callbacks.on_response(callbacks.ctx, handle, result);
    }

    return GGL_ERR_OK;
}

static GglError sub_fd_ready(void *ctx, uint64_t data) {
    (void) ctx;
    if (data > UINT32_MAX) {
        return GGL_ERR_FATAL;
    }

    uint32_t handle = (uint32_t) data;

    GglError ret = get_subscription_response(handle);
    if (ret != GGL_ERR_OK) {
        ggl_socket_close(&pool, handle);
    }

    return GGL_ERR_OK;
}

static void *subscription_thread(void *args) {
    assert(epoll_fd >= 0);

    GGL_LOGD("core-bus-client", "Started core bus subscription thread.");
    ggl_socket_epoll_run(epoll_fd, sub_fd_ready, args);
    GGL_LOGE("core-bus-client", "Core bus subscription thread exited.");
    return NULL;
}
