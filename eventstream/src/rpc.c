// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/eventstream/rpc.h"
#include "ggl/eventstream/decode.h"
#include "ggl/eventstream/types.h"
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <stdint.h>

GglError eventstream_get_common_headers(
    EventStreamMessage *msg, EventStreamCommonHeaders *out
) {
    int32_t message_type = -1;
    int32_t message_flags = 0;
    int32_t stream_id = 0;

    EventStreamHeaderIter iter = msg->headers;
    EventStreamHeader header;

    while (eventstream_header_next(&iter, &header) == GGL_ERR_OK) {
        if (ggl_buffer_eq(header.name, GGL_STR(":message-type"))) {
            if (header.value.type != EVENTSTREAM_INT32) {
                GGL_LOGE("ipc-server", ":message-type header not Int32.");
                return GGL_ERR_INVALID;
            }
            message_type = header.value.int32;
        } else if (ggl_buffer_eq(header.name, GGL_STR(":message-flags"))) {
            if (header.value.type != EVENTSTREAM_INT32) {
                GGL_LOGE("ipc-server", ":message-flags header not Int32.");
                return GGL_ERR_INVALID;
            }
            message_flags = header.value.int32;
        } else if (ggl_buffer_eq(header.name, GGL_STR(":stream-id"))) {
            if (header.value.type != EVENTSTREAM_INT32) {
                GGL_LOGE("ipc-server", ":stream-id header not Int32.");
                return GGL_ERR_INVALID;
            }
            stream_id = header.value.int32;
        }
    }

    out->message_type = message_type;
    out->message_flags = message_flags;
    out->stream_id = stream_id;
    return GGL_ERR_OK;
}
