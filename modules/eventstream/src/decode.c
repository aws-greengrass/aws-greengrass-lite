// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/eventstream/decode.h"
#include "crc32.h"
#include "ggl/eventstream/types.h"
#include <assert.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

static uint32_t read_be_uint32(GglBuffer buf) {
    assert(buf.len == 4);
    return ((uint32_t) buf.data[0] << 24) | ((uint32_t) buf.data[1] << 16)
        | ((uint32_t) buf.data[2] << 8) | ((uint32_t) buf.data[3]);
}

static int32_t read_be_int32(GglBuffer buf) {
    assert(buf.len == 4);
    uint32_t bytes = read_be_uint32(buf);
    int32_t ret;
    // memcpy necessary for well-defined type-punning
    memcpy(&ret, &bytes, sizeof(ret));
    return ret;
}

GglError eventstream_decode_prelude(
    GglBuffer buf, EventStreamPrelude *prelude
) {
    if (buf.len < 12) {
        return GGL_ERR_RANGE;
    }

    uint32_t crc = ggl_update_crc(0, ggl_buffer_substr(buf, 0, 8));

    uint32_t prelude_crc = read_be_uint32(ggl_buffer_substr(buf, 8, 12));

    if (crc != prelude_crc) {
        GGL_LOGE("Prelude CRC mismatch.");
        return GGL_ERR_PARSE;
    }

    uint32_t message_len = read_be_uint32(ggl_buffer_substr(buf, 0, 4));
    uint32_t headers_len = read_be_uint32(ggl_buffer_substr(buf, 4, 8));

    // message must at least have 12 byte prelude and 4 byte message crc
    if (message_len < 16) {
        GGL_LOGE("Prelude's message length below valid range.");
        return GGL_ERR_PARSE;
    }

    if (headers_len > message_len - 16) {
        GGL_LOGE("Prelude's header length does not fit in valid range.");
        return GGL_ERR_PARSE;
    }

    *prelude = (EventStreamPrelude) {
        .data_len = message_len - 12,
        .headers_len = headers_len,
        .crc = ggl_update_crc(prelude_crc, ggl_buffer_substr(buf, 8, 12)),
    };
    return GGL_ERR_OK;
}

/// Removes next header from buffer
static GglError take_header(GglBuffer *headers_buf) {
    assert(headers_buf != NULL);

    uint32_t pos = 0;

    if (headers_buf->len - pos < 1) {
        GGL_LOGE("Header parsing out of bounds.");
        return GGL_ERR_PARSE;
    }
    uint8_t header_name_len = headers_buf->data[pos];
    pos += 1;

    if (headers_buf->len - pos < header_name_len) {
        GGL_LOGE("Header parsing out of bounds.");
        return GGL_ERR_PARSE;
    }
    pos += header_name_len;

    if (headers_buf->len - pos < 1) {
        GGL_LOGE("Header parsing out of bounds.");
        return GGL_ERR_PARSE;
    }
    uint8_t header_value_type = headers_buf->data[pos];
    pos += 1;

    switch (header_value_type) {
    case EVENTSTREAM_INT32:
        if (headers_buf->len - pos < 4) {
            GGL_LOGE("Header parsing out of bounds.");
            return GGL_ERR_PARSE;
        }
        pos += 4;
        break;
    case EVENTSTREAM_STRING:
        if (headers_buf->len - pos < 2) {
            GGL_LOGE("Header parsing out of bounds.");
            return GGL_ERR_PARSE;
        }
        uint16_t value_len = (uint16_t) (headers_buf->data[pos] << 8)
            + headers_buf->data[pos + 1];
        pos += 2;

        if (headers_buf->len - pos < value_len) {
            GGL_LOGE("Header parsing out of bounds.");
            return GGL_ERR_PARSE;
        }
        pos += value_len;
        break;
    default:
        GGL_LOGE("Unsupported header value type.");
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

GglError eventstream_decode(
    const EventStreamPrelude *prelude,
    GglBuffer data_section,
    EventStreamMessage *msg
) {
    assert(msg != NULL);
    assert(data_section.len >= 4);

    GGL_LOGT("Decoding eventstream message.");

    uint32_t crc = ggl_update_crc(
        prelude->crc, ggl_buffer_substr(data_section, 0, data_section.len - 4)
    );

    uint32_t message_crc = read_be_uint32(
        ggl_buffer_substr(data_section, data_section.len - 4, data_section.len)
    );

    if (crc != message_crc) {
        GGL_LOGE("Message CRC mismatch %u %u.", crc, message_crc);
        return GGL_ERR_PARSE;
    }

    GglBuffer headers_buf
        = ggl_buffer_substr(data_section, 0, prelude->headers_len);
    GglBuffer payload = ggl_buffer_substr(
        data_section, prelude->headers_len, data_section.len - 4
    );

    assert(headers_buf.len == prelude->headers_len);

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

    // Print out headers at trace level
    EventStreamHeader header;
    while (eventstream_header_next(&header_iter, &header) == GGL_ERR_OK) {
        switch (header.value.type) {
        case EVENTSTREAM_INT32:
            GGL_LOGT(
                "Header: \"%.*s\" => %d",
                (int) header.name.len,
                header.name.data,
                header.value.int32
            );
            break;
        case EVENTSTREAM_STRING:
            GGL_LOGT(
                "Header: \"%.*s\" => (data not shown)",
                (int) header.name.len,
                header.name.data
            );
            break;
        }
    }

    GGL_LOGT("Successfully decoded eventstream message.");

    return GGL_ERR_OK;
}

GglError eventstream_header_next(
    EventStreamHeaderIter *headers, EventStreamHeader *header
) {
    assert(headers != NULL);
    assert(header != NULL);

    if (headers->count < 1) {
        return GGL_ERR_RANGE;
    }

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
        value.int32 = read_be_int32((GglBuffer) { .data = pos, .len = 4 });
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
    headers->count -= 1;

    *header = (EventStreamHeader) {
        .name = header_name,
        .value = value,
    };

    return GGL_ERR_OK;
}
