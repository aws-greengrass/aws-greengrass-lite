/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ggl/server.h"
#include "ggl/bump_alloc.h"
#include "ggl/defer.h"
#include "ggl/error.h"
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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>

/** Maximum number of outstanding responses.
 * Can be configured with `-DGGL_SERVER_MAX_OUTSTANDING=<N>`. */
#ifndef GGL_SERVER_MAX_OUTSTANDING
#define GGL_SERVER_MAX_OUTSTANDING 1
#endif

/** Buffer length for storing objects from decoding msgpack payloads.
 * Can be configured with `-DGGL_MSGPACK_DECODE_BUFFER_LEN=<N>`. */
#ifndef GGL_MSGPACK_DECODE_BUFFER_LEN
#define GGL_MSGPACK_DECODE_BUFFER_LEN (50 * sizeof(GglObject))
#endif

static pthread_mutex_t payload_array_mtx = PTHREAD_MUTEX_INITIALIZER;
static uint8_t payload_array[GGL_MSGPACK_MAX_MSG_LEN];
static uint8_t decode_array[GGL_MSGPACK_DECODE_BUFFER_LEN];

static pthread_mutex_t encode_array_mtx = PTHREAD_MUTEX_INITIALIZER;
static uint8_t encode_array[GGL_MSGPACK_MAX_MSG_LEN];

struct GglResponseHandle {
    int respfd;
    uint32_t msgid;
};

static GglResponseHandle handles[GGL_SERVER_MAX_OUTSTANDING];
pthread_mutex_t handles_mutex = PTHREAD_MUTEX_INITIALIZER;

static const int HANDLE_FREE = -2;
static const int HANDLE_UNINIT = -3;

__attribute__((constructor)) static void init_handles(void) {
    for (size_t i = 0; i < GGL_SERVER_MAX_OUTSTANDING; i++) {
        handles[i] = (GglResponseHandle) { .respfd = HANDLE_FREE };
    }
}

GGL_DEFINE_DEFER(
    release_handle, GglResponseHandle *, handle, (*handle)->respfd = HANDLE_FREE
)

static GglResponseHandle *get_free_handle(void) {
    while (true) {
        {
            pthread_mutex_lock(&handles_mutex);
            GGL_DEFER(pthread_mutex_unlock, handles_mutex);

            for (size_t i = 0; i < GGL_SERVER_MAX_OUTSTANDING; i++) {
                if (handles[i].respfd == HANDLE_FREE) {
                    handles[i] = (GglResponseHandle) {
                        .respfd = HANDLE_UNINIT,
                        .msgid = 0,
                    };
                    return &handles[i];
                }
            }
        }
        ggl_sleep(1);
    }
}

static GglError parse_incoming(
    GglBuffer buf,
    bool *needs_resp,
    uint32_t *msgid,
    GglBuffer *method,
    GglBuffer *params
) {
    assert(
        (needs_resp != NULL) && (msgid != NULL) && (method != NULL)
        && (params != NULL)
    );

    GglBuffer msg = buf;
    GglObject obj;

    GGL_TRY(ggl_msgpack_decode_lazy_noalloc(&msg, &obj));

    if ((obj.type != GGL_TYPE_LIST)) {
        GGL_LOGE("msgpack-rpc", "Received payload that is not an array.");
        return GGL_ERR_PARSE;
    }

    size_t mpk_len = obj.list.len;

    if (mpk_len < 3) {
        GGL_LOGE("msgpack-rpc", "Received payload that is too small array.");
        return GGL_ERR_PARSE;
    }

    // type
    GGL_TRY(ggl_msgpack_decode_lazy_noalloc(&msg, &obj));

    if (obj.type != GGL_TYPE_I64) {
        GGL_LOGE("msgpack-rpc", "Received payload type invalid.");
        return GGL_ERR_PARSE;
    }

    int64_t type = obj.i64;

    if (type == 0) {
        // request
        if (mpk_len != 4) {
            GGL_LOGE("msgpack-rpc", "Received payload not 4 element array.");
            return GGL_ERR_PARSE;
        }

        // msgid
        GGL_TRY(ggl_msgpack_decode_lazy_noalloc(&msg, &obj));

        if ((obj.type != GGL_TYPE_I64) || (obj.i64 < 0)
            || (obj.i64 > UINT32_MAX)) {
            GGL_LOGE("msgpack-rpc", "Received payload msgid invalid.");
            return GGL_ERR_PARSE;
        }

        *msgid = (uint32_t) obj.i64;
        *needs_resp = true;
    } else if (type == 2) {
        // notification
        if (mpk_len != 3) {
            GGL_LOGE("msgpack-rpc", "Received payload not 3 element array.");
            return GGL_ERR_PARSE;
        }
        *msgid = 0;
        *needs_resp = false;
    } else {
        GGL_LOGE(
            "msgpack-rpc",
            "Received payload type invalid: %lu",
            (long unsigned) type
        );
        return GGL_ERR_PARSE;
    }

    // method
    GGL_TRY(ggl_msgpack_decode_lazy_noalloc(&msg, &obj));

    if (obj.type != GGL_TYPE_BUF) {
        GGL_LOGE("msgpack-rpc", "Received non-raw method.");
        return GGL_ERR_PARSE;
    }

    *method = obj.buf;

    // params
    GglBuffer copy = msg;
    GGL_TRY(ggl_msgpack_decode_lazy_noalloc(&copy, &obj));

    if (obj.type != GGL_TYPE_LIST) {
        GGL_LOGE("msgpack-rpc", "Received non-array params.");
        return GGL_ERR_PARSE;
    }

    *params = msg;

    return 0;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
noreturn void ggl_listen(GglBuffer path, void *ctx) {
    while (true) {
        int sockfd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
        if (sockfd == -1) {
            int err = errno;
            GGL_LOGE("msgpack-rpc", "Failed to create socket: %d.", err);
            ggl_sleep(5);
            continue;
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

        if (bind(sockfd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
            int err = errno;
            GGL_LOGE("msgpack-rpc", "Failed to bind socket: %d.", err);
            ggl_sleep(5);
            continue;
        }

        if (listen(sockfd, 20) == -1) {
            int err = errno;
            GGL_LOGE("msgpack-rpc", "Failed to listen socket: %d.", err);
            ggl_sleep(5);
            continue;
        }

        while (true) {
            int clientfd = accept(sockfd, NULL, NULL);
            if (clientfd == -1) {
                int err = errno;
                GGL_LOGE("msgpack-rpc", "Failed to accept on socket: %d.", err);
                break;
            }
            GGL_DEFER(close, clientfd);

            while (true) {
                pthread_mutex_lock(&payload_array_mtx);
                GGL_DEFER(pthread_mutex_unlock, payload_array_mtx);

                GglBuffer recv_buffer = GGL_BUF(payload_array);

                ssize_t sys_ret = recv(
                    clientfd, recv_buffer.data, recv_buffer.len, MSG_TRUNC
                );
                if (sys_ret < 0) {
                    int err = errno;
                    GGL_LOGE(
                        "msgpack-rpc", "Failed to recv from client: %d.", err
                    );
                    break;
                }

                recv_buffer.len = (size_t) sys_ret;

                if ((size_t) sys_ret > recv_buffer.len) {
                    // TODO: respond with server error if request
                    GGL_LOGE(
                        "msgpack-rpc",
                        "Payload too large: size %zu, max %zu",
                        (size_t) sys_ret,
                        recv_buffer.len
                    );
                    continue;
                }

                if ((size_t) sys_ret == 0) {
                    GGL_LOGI("msgpack-rpc", "Connection closed.");
                    break;
                }

                bool needs_resp;
                uint32_t msgid;
                GglBuffer method;
                GglBuffer params_buf;

                GglError ret = parse_incoming(
                    recv_buffer, &needs_resp, &msgid, &method, &params_buf
                );
                if (ret != GGL_ERR_OK) {
                    break;
                }

                GglBumpAlloc decode_mem
                    = ggl_bump_alloc_init(GGL_BUF(decode_array));
                GglObject params_obj;

                ret = ggl_msgpack_decode(
                    &decode_mem.alloc, params_buf, &params_obj
                );
                if (ret != GGL_ERR_OK) {
                    GGL_LOGE(
                        "msgpack-rpc", "Failed decoding incoming payload."
                    );
                    // TODO: Send server error
                    break;
                };

                if (params_obj.type != GGL_TYPE_LIST) {
                    GGL_LOGE(
                        "msgpack-rpc", "Incoming payload params not list."
                    );
                    // TODO: Send server error
                    break;
                }

                GglList params = params_obj.list;

                GglResponseHandle *handle = NULL;

                if (needs_resp) {
                    handle = get_free_handle();
                    *handle = (GglResponseHandle) {
                        .msgid = msgid,
                        .respfd = clientfd,
                    };
                }

                ggl_receive_callback(ctx, method, params, handle);
            }
        }
    }
}

void ggl_respond(GglResponseHandle *handle, GglError error, GglObject value) {
    if (handle == NULL) {
        return;
    }

    GGL_DEFER(release_handle, handle);

    GglObject payload = GGL_OBJ_LIST(
        GGL_OBJ_I64(1),
        GGL_OBJ_I64(handle->msgid),
        (error != 0) ? GGL_OBJ_I64(error) : GGL_OBJ_NULL(),
        (error != 0) ? GGL_OBJ_NULL() : value
    );

    pthread_mutex_lock(&encode_array_mtx);
    GGL_DEFER(pthread_mutex_unlock, encode_array_mtx);

    GglBuffer encoded = GGL_BUF(encode_array);

    GglError ret = ggl_msgpack_encode(payload, &encoded);
    if (ret != 0) {
        GGL_LOGE("msgpack-rpc", "Failed to encode response.");
        return;
    }

    ssize_t result = send(handle->respfd, encoded.data, encoded.len, 0);
    if (result <= 0) {
        int err = errno;
        GGL_LOGE("msgpack-rpc", "Failed to send response: %d.", err);
    }
}
