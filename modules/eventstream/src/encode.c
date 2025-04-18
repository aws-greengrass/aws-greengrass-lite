// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/eventstream/encode.h"
#include "crc32.h"
#include "ggl/eventstream/types.h"
#include <assert.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/io.h>
#include <ggl/log.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

static void write_be_u32(uint32_t val, uint8_t dest[4]) {
    dest[0] = (uint8_t) (val >> 24);
    dest[1] = (uint8_t) ((val >> 16) & 0xFF);
    dest[2] = (uint8_t) ((val >> 8) & 0xFF);
    dest[3] = (uint8_t) (val & 0xFF);
}

static GglError header_encode(GglWriter out, EventStreamHeader header) {
    if (header.name.len > UINT8_MAX) {
        GGL_LOGE("Header name field too long.");
        return GGL_ERR_RANGE;
    }
    uint8_t header_name_len[1] = { (uint8_t) header.name.len };
    GglError ret = ggl_writer_call(out, GGL_BUF(header_name_len));
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Insufficent buffer space to encode packet.");
        return ret;
    }

    ret = ggl_writer_call(out, header.name);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Insufficent buffer space to encode packet.");
        return ret;
    }

    uint8_t header_value_type[1] = { (uint8_t) header.value.type };
    ret = ggl_writer_call(out, GGL_BUF(header_value_type));
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Insufficent buffer space to encode packet.");
        return ret;
    }

    switch (header.value.type) {
    case EVENTSTREAM_INT32: {
        uint32_t val;
        memcpy(&val, &header.value.int32, 4);

        uint8_t data[4];
        write_be_u32(val, data);

        ret = ggl_writer_call(out, GGL_BUF(data));
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Insufficent buffer space to encode packet.");
            return ret;
        }
    } break;
    case EVENTSTREAM_STRING: {
        GglBuffer str = header.value.string;

        if (str.len > UINT16_MAX) {
            GGL_LOGE("String length exceeds eventstream limits.");
            return GGL_ERR_RANGE;
        }
        uint16_t str_len = (uint16_t) str.len;

        uint8_t len_data[2];
        len_data[0] = (uint8_t) (str_len >> 8);
        len_data[1] = (uint8_t) (str_len & 0xFF);

        ret = ggl_writer_call(out, GGL_BUF(len_data));
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Insufficent buffer space to encode packet.");
            return ret;
        }

        ret = ggl_writer_call(out, str);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Insufficent buffer space to encode packet.");
            return ret;
        }
    } break;
    default:
        GGL_LOGE("Unhandled header value type.");
        return GGL_ERR_PARSE;
    }

    return GGL_ERR_OK;
}

GglError eventstream_encode(
    GglBuffer *buf,
    const EventStreamHeader *headers,
    size_t header_count,
    GglReader payload
) {
    assert((headers == NULL) ? (header_count == 0) : true);

    if (buf->len > UINT32_MAX) {
        buf->len = UINT32_MAX;
    }

    GglBuffer buf_copy = *buf;

    if (buf_copy.len < 12) {
        GGL_LOGE("Insufficent buffer space to encode packet.");
        return GGL_ERR_NOMEM;
    }
    uint8_t *prelude = buf_copy.data;
    buf_copy = ggl_buffer_substr(buf_copy, 12, SIZE_MAX);

    uint8_t *message_len_p = prelude;
    uint8_t *headers_len_p = &prelude[4];
    uint8_t *prelude_crc_p = &prelude[8];

    uint32_t headers_len = 0;

    if (headers != NULL) {
        uint8_t *headers_start = buf_copy.data;
        GglWriter headers_writer = ggl_buf_writer(&buf_copy);

        for (size_t i = 0; i < header_count; i++) {
            GglError err = header_encode(headers_writer, headers[i]);
            if (err != GGL_ERR_OK) {
                return err;
            }
        }

        headers_len = (uint32_t) (buf_copy.data - headers_start);
    }

    write_be_u32(headers_len, headers_len_p);

    GglBuffer payload_buf = buf_copy;
    GglError err = ggl_reader_call(payload, &payload_buf);
    if (err != GGL_ERR_OK) {
        return err;
    }
    buf_copy = ggl_buffer_substr(buf_copy, payload_buf.len, SIZE_MAX);

    uint32_t message_len = 12 + headers_len + (uint32_t) payload_buf.len + 4;

    write_be_u32(message_len, message_len_p);

    uint32_t prelude_crc
        = ggl_update_crc(0, (GglBuffer) { .data = prelude, .len = 8 });

    write_be_u32(prelude_crc, prelude_crc_p);

    uint32_t message_crc = ggl_update_crc(
        prelude_crc,
        (GglBuffer) { .data = &prelude[8], .len = message_len - 8 - 4 }
    );

    if (buf_copy.len < 4) {
        GGL_LOGE("Insufficent buffer space to encode packet.");
        return GGL_ERR_NOMEM;
    }
    uint8_t *message_crc_p = buf_copy.data;

    write_be_u32(message_crc, message_crc_p);

    buf->len = message_len;

    return GGL_ERR_OK;
}
