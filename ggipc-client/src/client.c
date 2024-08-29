// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggipc/client.h"
#include <assert.h>
#include <ggl/buffer.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/eventstream/decode.h>
#include <ggl/eventstream/encode.h>
#include <ggl/eventstream/rpc.h>
#include <ggl/eventstream/types.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/socket.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

/// Maximum size of eventstream packet.
/// Can be configured with `-DGGL_IPC_MAX_MSG_LEN=<N>`.
#ifndef GGL_IPC_MAX_MSG_LEN
#define GGL_IPC_MAX_MSG_LEN 10000
#endif

static uint8_t payload_array[GGL_IPC_MAX_MSG_LEN];

// TODO: Refactor this function
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
GglError ggipc_connect_auth(GglBuffer socket_path, GglBuffer *svcuid, int *fd) {
    int conn = -1;
    GglError ret = ggl_connect(socket_path, &conn);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    GGL_DEFER(close, conn);

    GglBuffer send_buffer = GGL_BUF(payload_array);

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

    ret = eventstream_encode(&send_buffer, headers, headers_len, NULL, NULL);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = ggl_write_exact(conn, send_buffer);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GglBuffer recv_buffer = GGL_BUF(payload_array);

    GglBuffer prelude_buf = ggl_buffer_substr(recv_buffer, 0, 12);
    assert(prelude_buf.len == 12);

    ret = ggl_read_exact(conn, prelude_buf);
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
            "ggipc-client",
            "EventStream packet does not fit in core bus buffer size."
        );
        return GGL_ERR_NOMEM;
    }

    GglBuffer data_section
        = ggl_buffer_substr(recv_buffer, 0, prelude.data_len);

    ret = ggl_read_exact(conn, data_section);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    EventStreamMessage msg = { 0 };
    ret = eventstream_decode(&prelude, data_section, &msg);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    EventStreamCommonHeaders common_headers;
    ret = eventstream_get_common_headers(&msg, &common_headers);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (common_headers.message_type != EVENTSTREAM_CONNECT_ACK) {
        GGL_LOGE("ggipc-client", "Connection response not an ack.");
        return GGL_ERR_FAILURE;
    }

    if ((common_headers.message_flags & EVENTSTREAM_CONNECTION_ACCEPTED) == 0) {
        GGL_LOGE("ggipc-client", "Connection response missing accepted flag.");
        return GGL_ERR_FAILURE;
    }

    EventStreamHeaderIter iter = msg.headers;
    EventStreamHeader header;

    while (eventstream_header_next(&iter, &header) == GGL_ERR_OK) {
        if (ggl_buffer_eq(header.name, GGL_STR("svcuid"))) {
            if (header.value.type != EVENTSTREAM_STRING) {
                GGL_LOGE("ggipc-client", "Response svcuid header not string.");
                return GGL_ERR_INVALID;
            }

            if (svcuid != NULL) {
                if (svcuid->len < header.value.string.len) {
                    GGL_LOGE(
                        "ggipc-client", "Insufficient buffer space for svcuid."
                    );
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
                GGL_DEFER_CANCEL(conn);
                *fd = conn;
            }

            return GGL_ERR_OK;
        }
    }

    GGL_LOGE("ggipc-client", "Response missing svcuid header.");
    return GGL_ERR_FAILURE;
}
