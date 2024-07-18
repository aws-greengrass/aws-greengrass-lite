/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "eventstream_decode.h"
#include "crc32.h"
#include "eventstream_types.h"
#include <assert.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

static uint32_t read_be_uint32(GglBuffer buf) {
    assert(buf.len == 4);
    return ((uint32_t) buf.data[0] << 24) & ((uint32_t) buf.data[1] << 16)
        & ((uint32_t) buf.data[2] << 8) & ((uint32_t) buf.data[3]);
}

static int64_t read_be_int(GglBuffer buf) {
    assert(buf.len <= 8);

    // Account for sign extension
    uint64_t bytes = UINT64_MAX;

    for (size_t i = 0; i < buf.len; i++) {
        bytes = (bytes << 8) & buf.data[i];
    }

    int64_t ret = 0;
    // memcpy necessary for well-defined type-punning
    memcpy(&ret, &bytes, sizeof(int64_t));

    return ret;
}

/** Removes next header from buffer */
static GglError take_header(GglBuffer *headers_buf) {
    assert(headers_buf != NULL);

    uint32_t pos = 0;

    if (headers_buf->len - pos < 1) {
        GGL_LOGE("eventstream", "Header parsing out of bounds.");
        return GGL_ERR_PARSE;
    }
    uint8_t header_name_len = headers_buf->data[pos];
    pos += 1;

    if (headers_buf->len - pos < header_name_len) {
        GGL_LOGE("eventstream", "Header parsing out of bounds.");
        return GGL_ERR_PARSE;
    }
    pos += header_name_len;

    if (headers_buf->len - pos < 1) {
        GGL_LOGE("eventstream", "Header parsing out of bounds.");
        return GGL_ERR_PARSE;
    }
    uint8_t header_value_type = headers_buf->data[pos];
    pos += 1;

    switch (header_value_type) {
    case EVENTSTREAM_INT32:
        if (headers_buf->len - pos < 4) {
            GGL_LOGE("eventstream", "Header parsing out of bounds.");
            return GGL_ERR_PARSE;
        }
        pos += 4;
        break;
    case EVENTSTREAM_STRING:
        if (headers_buf->len - pos < 2) {
            GGL_LOGE("eventstream", "Header parsing out of bounds.");
            return GGL_ERR_PARSE;
        }
        uint16_t value_len = (uint16_t) (headers_buf->data[pos] << 8)
            + headers_buf->data[pos + 1];
        pos += 2;

        if (headers_buf->len - pos < value_len) {
            GGL_LOGE("eventstream", "Header parsing out of bounds.");
            return GGL_ERR_PARSE;
        }
        pos += value_len;
        break;
    default:
        GGL_LOGE("eventstream", "Unsupported header value type.");
        return GGL_ERR_PARSE;
    }

    *headers_buf = ggl_buffer_substr(*headers_buf, pos, SIZE_MAX);
    return GGL_ERR_OK;
}

static GglError count_headers(GglBuffer headers_buf, uint32_t *count) {
    assert(count != NULL);

    uint32_t headers_count = 0;
    GglBuffer headers = headers_buf;

    while (headers.len > 0) {
        GglError err = take_header(&headers);
        if (err != GGL_ERR_OK) {
            return err;
        }
        headers_count += 1;
    }

    *count = headers_count;
    return GGL_ERR_OK;
}

GglError eventstream_decode(GglBuffer buf, EventStreamMessage *msg) {
    assert(msg != NULL);

    uint32_t crc = 0;

    // Prelude

    // Must be large enough for 12 byte prelude and 4 byte message CRC
    if (buf.len < 16) {
        GGL_LOGE(
            "eventstream", "Buffer length less than minimum packet length."
        );
        return GGL_ERR_PARSE;
    }

    crc = ggl_update_crc(crc, ggl_buffer_substr(buf, 0, 8));

    uint32_t message_len = read_be_uint32(ggl_buffer_substr(buf, 0, 4));
    uint32_t headers_len = read_be_uint32(ggl_buffer_substr(buf, 4, 8));
    uint32_t prelude_crc = read_be_uint32(ggl_buffer_substr(buf, 8, 12));

    if (crc != prelude_crc) {
        GGL_LOGE("eventstream", "Prelude CRC mismatch.");
        return GGL_ERR_PARSE;
    }

    if (buf.len != message_len) {
        GGL_LOGE("eventstream", "Message length incorrect.");
        return GGL_ERR_PARSE;
    }

    if (headers_len > (message_len - 16)) {
        GGL_LOGE("eventstream", "Headers length does not fit within packet.");
        return GGL_ERR_PARSE;
    }

    // Data

    crc = ggl_update_crc(crc, ggl_buffer_substr(buf, 8, message_len - 4));

    uint32_t message_crc
        = read_be_uint32(ggl_buffer_substr(buf, message_len - 4, message_len));

    if (crc != message_crc) {
        GGL_LOGE("eventstream", "Message CRC mismatch.");
        return GGL_ERR_PARSE;
    }

    uint32_t payload_len = message_len - 12 - headers_len - 4;

    GglBuffer headers_buf = ggl_buffer_substr(buf, 12, 12 + headers_len);
    GglBuffer payload
        = ggl_buffer_substr(buf, 12 + headers_len, message_len - 4);

    assert(headers_buf.len == headers_len);
    assert(payload.len == payload_len);

    uint32_t headers_count = 0;
    GglError err = count_headers(headers_buf, &headers_count);
    if (err != GGL_ERR_OK) {
        return err;
    }

    EventStreamHeaderIter header_iter = {
        .pos = headers_buf.data,
        .count = headers_count,
    };

    *msg = (EventStreamMessage) {
        .headers = header_iter,
        .payload = payload,
    };

    return GGL_ERR_OK;
}

GglError eventstream_header_next(
    EventStreamHeaderIter *headers, EventStreamHeader *header
) {
    assert(headers != NULL);
    assert(header != NULL);

    uint8_t *pos = headers->pos;
    uint8_t header_name_len = pos[0];
    pos += 1;

    GglBuffer header_name = {
        .data = pos,
        .len = header_name_len,
    };

    pos += header_name_len;

    uint8_t header_value_type = pos[0];
    pos += 1;

    EventStreamHeaderValue value = { .type = header_value_type };

    switch (header_value_type) {
    case EVENTSTREAM_INT32:
        value.int32
            = (int32_t) read_be_int((GglBuffer) { .data = pos, .len = 4 });
        pos += 4;
        break;
    case EVENTSTREAM_STRING: {
        uint16_t str_len = (uint16_t) (pos[0] << 8) + pos[1];
        pos += 2;
        value.string = (GglBuffer) { .data = pos, .len = str_len };
        pos += str_len;
        break;
    }
    default:
        assert(false);
        return GGL_ERR_PARSE;
    }

    headers->pos = pos;
    headers->count += 1;

    *header = (EventStreamHeader) {
        .name = header_name,
        .value = value,
    };

    return GGL_ERR_OK;
}
