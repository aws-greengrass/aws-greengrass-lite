/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ggl/client.h"
#include "ggl/alloc.h"
#include "ggl/defer.h"
#include "ggl/log.h"
#include "ggl/object.h"
#include "ggl/utils.h"
#include "msgpack.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/** Maximum number of simultaneous client connections.
 * Can be configured with `-DGGL_CLIENT_CONN_MAX=<N>`. */
#ifndef GGL_CLIENT_CONN_MAX
#define GGL_CLIENT_CONN_MAX 1
#endif

static pthread_mutex_t payload_array_mtx = PTHREAD_MUTEX_INITIALIZER;
static uint8_t payload_array[GGL_MSGPACK_MAX_MSG_LEN];

struct GglConn {
    int sockfd;
    uint32_t counter;
};

static GglConn conns[GGL_CLIENT_CONN_MAX];
pthread_mutex_t conns_mutex = PTHREAD_MUTEX_INITIALIZER;

static const int CONNS_FREE = -2;
static const int CONNS_UNINIT = -3;

__attribute__((constructor)) static void init_conns(void) {
    for (size_t i = 0; i < GGL_CLIENT_CONN_MAX; i++) {
        conns[i] = (GglConn) { .sockfd = CONNS_FREE };
    }
}

static GglConn *get_free_conn(void) {
    pthread_mutex_lock(&conns_mutex);
    GGL_DEFER(pthread_mutex_unlock, conns_mutex);

    for (size_t i = 0; i < GGL_CLIENT_CONN_MAX; i++) {
        if (conns[i].sockfd == CONNS_FREE) {
            conns[i] = (GglConn) {
                .sockfd = CONNS_UNINIT,
                .counter = 0,
            };
            return &conns[i];
        }
    }
    return NULL;
}

__attribute__((weak)) int ggl_connect(GglBuffer path, GglConn **conn) {
    assert(conn != NULL);

    *conn = NULL;

    int sockfd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (sockfd == -1) {
        int err = errno;
        GGL_LOGE("msgpack-rpc", "Failed to create socket: %d.", err);
        return err;
    }
    GGL_DEFER(close, sockfd);

    struct sockaddr_un addr = { .sun_family = AF_UNIX, .sun_path = { 0 } };

    // Skipping first byte (makes socket in abstract namespace)

    size_t copy_len = path.len <= sizeof(addr.sun_path) - 1
        ? path.len
        : sizeof(addr.sun_path) - 1;

    if (copy_len < path.len) {
        GGL_LOGW(
            "msgpack-rpc",
            "Truncating path to %u bytes [%.*s]",
            (unsigned) copy_len,
            (int) path.len,
            (char *) path.data
        );
    }

    memcpy(&addr.sun_path[1], path.data, copy_len);

    if (connect(sockfd, (const struct sockaddr *) &addr, sizeof(addr)) == -1) {
        int err = errno;
        GGL_LOGW("msgpack-rpc", "Failed to connect to server: %d.", err);
        return err;
    }

    GglConn *c = get_free_conn();
    if (c == NULL) {
        return ENOBUFS;
    }

    GGL_DEFER_CANCEL(sockfd);
    *c = (GglConn) { .sockfd = sockfd, .counter = 0 };
    *conn = c;
    return 0;
}

void ggl_close(GglConn *conn) {
    assert(conn != NULL);

    pthread_mutex_lock(&conns_mutex);
    GGL_DEFER(pthread_mutex_unlock, conns_mutex);

    close(conn->sockfd);
    conn->sockfd = CONNS_FREE;
}

static int parse_incoming(
    GglBuffer buf, uint32_t *msgid, bool *error, GglBuffer *value
) {
    GglBuffer msg = buf;
    GglObject obj;

    int ret = ggl_msgpack_decode_lazy_noalloc(&msg, &obj);
    if (ret != 0) {
        return ret;
    }

    if ((obj.type != GGL_TYPE_LIST) || (obj.list.len != 4)) {
        GGL_LOGE("msgpack-rpc", "Received payload not 4 element array.");
        return EPROTO;
    }

    // payload type
    ret = ggl_msgpack_decode_lazy_noalloc(&msg, &obj);
    if (ret != 0) {
        return ret;
    }

    if ((obj.type != GGL_TYPE_I64) || (obj.i64 != 1)) {
        GGL_LOGE("msgpack-rpc", "Received payload type invalid.");
        return EPROTO;
    }

    // msgid
    ret = ggl_msgpack_decode_lazy_noalloc(&msg, &obj);
    if (ret != 0) {
        return ret;
    }

    if ((obj.type != GGL_TYPE_I64) || (obj.i64 < 0) || (obj.i64 > UINT32_MAX)) {
        GGL_LOGE("msgpack-rpc", "Received payload msgid invalid.");
        return EPROTO;
    }

    *msgid = (uint32_t) obj.i64;

    // error
    GglBuffer copy = msg;
    ret = ggl_msgpack_decode_lazy_noalloc(&copy, &obj);
    if (ret != 0) {
        return ret;
    }

    if (obj.type != GGL_TYPE_NULL) {
        msg.len -= 1; // Account for result (should be nil, aka 1 byte)
        *error = true;
        *value = msg;
        return 0;
    }

    // result
    *error = false;
    *value = copy;
    return 0;
}

int ggl_call(
    GglConn *conn,
    GglBuffer method,
    GglList params,
    GglAlloc *alloc,
    GglObject *result
) {
    assert(conn != NULL);

    uint32_t msgid;
    msgid = conn->counter++;

    GglObject payload = GGL_OBJ_LIST(
        GGL_OBJ_I64(0), GGL_OBJ_I64(msgid), GGL_OBJ(method), GGL_OBJ(params)
    );

    ssize_t sys_ret;

    {
        pthread_mutex_lock(&payload_array_mtx);
        GGL_DEFER(pthread_mutex_unlock, payload_array_mtx);

        GglBuffer send_buffer = GGL_BUF(payload_array);

        int ret = ggl_msgpack_encode(payload, &send_buffer);
        if (ret != 0) {
            return ret;
        }

        sys_ret = send(conn->sockfd, send_buffer.data, send_buffer.len, 0);
    }

    if (sys_ret < 0) {
        int err = errno;
        GGL_LOGE("msgpack-rpc", "Failed to send: %d.", err);
        return err;
    }

    while (true) {
        pthread_mutex_lock(&payload_array_mtx);
        GGL_DEFER(pthread_mutex_unlock, payload_array_mtx);

        GglBuffer recv_buffer = GGL_BUF(payload_array);

        sys_ret = recv(
            conn->sockfd,
            recv_buffer.data,
            recv_buffer.len,
            MSG_PEEK | MSG_TRUNC
        );

        if (sys_ret < 0) {
            int err = errno;
            if (err == EINTR) {
                continue;
            }
            GGL_LOGE("msgpack-rpc", "Failed recv: %d.", err);
            return err;
        }

        if ((size_t) sys_ret > recv_buffer.len) {
            GGL_LOGE(
                "msgpack-rpc",
                "Payload too large: size %zu, max %zu",
                (size_t) sys_ret,
                recv_buffer.len
            );
            return EMSGSIZE;
        }

        recv_buffer.len = (size_t) sys_ret;

        // Must not alloc here, as we want to claim message even if ENOMEM
        uint32_t ret_id;
        bool error;
        GglBuffer result_buf;
        int ret = parse_incoming(recv_buffer, &ret_id, &error, &result_buf);
        if (ret != 0) {
            return ret;
        }

        if (ret_id != msgid) {
            // not ours
            GGL_DEFER_FORCE(payload_array_mtx);
            ggl_sleep(1);
            continue;
        }

        // claim message
        (void) recv(conn->sockfd, NULL, 0, MSG_TRUNC);

        ret = ggl_msgpack_decode(alloc, result_buf, result);
        if (ret != 0) {
            GGL_LOGE("msgpack-rpc", "Failed to decode payload response.");
            return EPROTO;
        }

        return 0;
    }
}

int ggl_notify(GglConn *conn, GglBuffer method, GglList params) {
    assert(conn != NULL);

    GglObject payload
        = GGL_OBJ_LIST(GGL_OBJ_I64(2), GGL_OBJ(method), GGL_OBJ(params));

    ssize_t sys_ret;

    {
        pthread_mutex_lock(&payload_array_mtx);
        GGL_DEFER(pthread_mutex_unlock, payload_array_mtx);

        GglBuffer send_buffer = GGL_BUF(payload_array);

        int ret = ggl_msgpack_encode(payload, &send_buffer);
        if (ret != 0) {
            return ret;
        }

        sys_ret = send(conn->sockfd, send_buffer.data, send_buffer.len, 0);
    }

    if (sys_ret < 0) {
        int err = errno;
        GGL_LOGE("msgpack-rpc", "Failed to send: %d.", err);
        return err;
    }

    return 0;
}
