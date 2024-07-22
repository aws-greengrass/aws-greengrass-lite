/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "msgpack.h"
#include <assert.h>
#include <errno.h>
#include <ggl/alloc.h>
#include <ggl/client.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/utils.h>
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

static pthread_mutex_t payload_array_mtx = PTHREAD_MUTEX_INITIALIZER;
static uint8_t payload_array[GGL_MSGPACK_MAX_MSG_LEN];

static GglError interface_connect(GglBuffer interface, int *conn) {
    assert(conn != NULL);

    int sockfd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (sockfd == -1) {
        int err = errno;
        GGL_LOGE("msgpack-rpc", "Failed to create socket: %d.", err);
        return GGL_ERR_FATAL;
    }
    GGL_DEFER(close, sockfd);

    struct sockaddr_un addr = { .sun_family = AF_UNIX, .sun_path = { 0 } };

    // Skipping first byte (makes socket in abstract namespace)

    size_t copy_len = interface.len <= sizeof(addr.sun_path) - 1
        ? interface.len
        : sizeof(addr.sun_path) - 1;

    if (copy_len < interface.len) {
        GGL_LOGW(
            "msgpack-rpc",
            "Truncating path to %u bytes [%.*s]",
            (unsigned) copy_len,
            (int) interface.len,
            (char *) interface.data
        );
    }

    memcpy(&addr.sun_path[1], interface.data, copy_len);

    if (connect(sockfd, (const struct sockaddr *) &addr, sizeof(addr)) == -1) {
        int err = errno;
        GGL_LOGW("msgpack-rpc", "Failed to connect to server: %d.", err);
        return GGL_ERR_FAILURE;
    }

    GGL_DEFER_CANCEL(sockfd);
    *conn = sockfd;
    return GGL_ERR_OK;
}

static GglError parse_incoming(
    GglBuffer buf, uint32_t *msgid, bool *error, GglBuffer *value
) {
    GglBuffer msg = buf;
    GglObject obj;

    GglError ret = ggl_msgpack_decode_lazy_noalloc(&msg, &obj);
    if (ret != 0) {
        return ret;
    }

    if ((obj.type != GGL_TYPE_LIST) || (obj.list.len != 4)) {
        GGL_LOGE("msgpack-rpc", "Received payload not 4 element array.");
        return GGL_ERR_PARSE;
    }

    // payload type
    ret = ggl_msgpack_decode_lazy_noalloc(&msg, &obj);
    if (ret != 0) {
        return ret;
    }

    if ((obj.type != GGL_TYPE_I64) || (obj.i64 != 1)) {
        GGL_LOGE("msgpack-rpc", "Received payload type invalid.");
        return GGL_ERR_PARSE;
    }

    // msgid
    ret = ggl_msgpack_decode_lazy_noalloc(&msg, &obj);
    if (ret != 0) {
        return ret;
    }

    if ((obj.type != GGL_TYPE_I64) || (obj.i64 < 0) || (obj.i64 > UINT32_MAX)) {
        GGL_LOGE("msgpack-rpc", "Received payload msgid invalid.");
        return GGL_ERR_PARSE;
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

GglError ggl_call(
    GglBuffer interface,
    GglBuffer method,
    GglMap params,
    GglAlloc *alloc,
    GglObject *result
) {
    int conn = -1;
    GglError ret = interface_connect(interface, &conn);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    GGL_DEFER(close, conn);

    uint32_t msgid = 1;
    GglObject payload = GGL_OBJ_LIST(
        GGL_OBJ_I64(0),
        GGL_OBJ_I64(msgid),
        GGL_OBJ(method),
        GGL_OBJ_LIST(GGL_OBJ(params))
    );

    ssize_t sys_ret;

    {
        pthread_mutex_lock(&payload_array_mtx);
        GGL_DEFER(pthread_mutex_unlock, payload_array_mtx);

        GglBuffer send_buffer = GGL_BUF(payload_array);

        ret = ggl_msgpack_encode(payload, &send_buffer);
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        sys_ret = send(conn, send_buffer.data, send_buffer.len, 0);
    }

    if (sys_ret < 0) {
        int err = errno;
        GGL_LOGE("msgpack-rpc", "Failed to send: %d.", err);
        return GGL_ERR_FAILURE;
    }

    while (true) {
        pthread_mutex_lock(&payload_array_mtx);
        GGL_DEFER(pthread_mutex_unlock, payload_array_mtx);

        GglBuffer recv_buffer = GGL_BUF(payload_array);

        sys_ret = recv(
            conn, recv_buffer.data, recv_buffer.len, MSG_PEEK | MSG_TRUNC
        );

        if (sys_ret < 0) {
            int err = errno;
            if (err == EINTR) {
                continue;
            }
            GGL_LOGE("msgpack-rpc", "Failed recv: %d.", err);
            return GGL_ERR_FAILURE;
        }

        if ((size_t) sys_ret > recv_buffer.len) {
            GGL_LOGE(
                "msgpack-rpc",
                "Payload too large: size %zu, max %zu",
                (size_t) sys_ret,
                recv_buffer.len
            );
            return GGL_ERR_NOMEM;
        }

        recv_buffer.len = (size_t) sys_ret;

        // Must not alloc here, as we want to claim message even if ENOMEM
        uint32_t ret_id;
        bool error;
        GglBuffer result_buf;
        ret = parse_incoming(recv_buffer, &ret_id, &error, &result_buf);
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
        (void) recv(conn, NULL, 0, MSG_TRUNC);

        ret = ggl_msgpack_decode(alloc, result_buf, result);
        if (ret != 0) {
            GGL_LOGE("msgpack-rpc", "Failed to decode payload response.");
            return GGL_ERR_PARSE;
        }

        return 0;
    }
}

GglError ggl_notify(GglBuffer interface, GglBuffer method, GglMap params) {
    int conn = -1;
    GglError ret = interface_connect(interface, &conn);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    GGL_DEFER(close, conn);

    GglObject payload = GGL_OBJ_LIST(
        GGL_OBJ_I64(2), GGL_OBJ(method), GGL_OBJ_LIST(GGL_OBJ(params))
    );

    ssize_t sys_ret;

    {
        pthread_mutex_lock(&payload_array_mtx);
        GGL_DEFER(pthread_mutex_unlock, payload_array_mtx);

        GglBuffer send_buffer = GGL_BUF(payload_array);

        ret = ggl_msgpack_encode(payload, &send_buffer);
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        sys_ret = send(conn, send_buffer.data, send_buffer.len, 0);
    }

    if (sys_ret < 0) {
        int err = errno;
        GGL_LOGE("msgpack-rpc", "Failed to send: %d.", err);
        return GGL_ERR_FAILURE;
    }

    return GGL_ERR_OK;
}
