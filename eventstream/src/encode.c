/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ggl/eventstream/encode.h"
#include "crc32.h"
#include "ggl/eventstream/types.h"
#include <assert.h>
#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <string.h>
#include <stdint.h>

static void write_be_u32(uint32_t val, uint8_t *dest) {
    dest[0] = (uint8_t) (val >> 24);
    dest[1] = (uint8_t) ((val >> 16) & 0xFF);
    dest[2] = (uint8_t) ((val >> 8) & 0xFF);
    dest[3] = (uint8_t) (val & 0xFF);
}

static GglError header_encode(GglAlloc *alloc, EventStreamHeader header) {
    assert(alloc != NULL);

    if (header.name.len > UINT8_MAX) {
        GGL_LOGE("eventstream", "Header name field too long.");
        return GGL_ERR_RANGE;
    }

    uint8_t *header_name_len_p = GGL_ALLOCN(alloc, uint8_t, 1);
    if (header_name_len_p == NULL) {
        GGL_LOGE("eventstream", "Insufficent buffer space to encode packet.");
        return GGL_ERR_NOMEM;
    }

    *header_name_len_p = (uint8_t) header.name.len;

    uint8_t *header_name_p = GGL_ALLOCN(alloc, uint8_t, header.name.len);
    if (header_name_p == NULL) {
        GGL_LOGE("eventstream", "Insufficent buffer space to encode packet.");
        return GGL_ERR_NOMEM;
    }
    memcpy(header_name_p, header.name.data, header.name.len);

    uint8_t *header_value_type_p = GGL_ALLOCN(alloc, uint8_t, 1);

    if (header_value_type_p == NULL) {
        GGL_LOGE("eventstream", "Insufficent buffer space to encode packet.");
        return GGL_ERR_NOMEM;
    }

    *header_value_type_p = (uint8_t) header.value.type;

    switch (header.value.type) {
    case EVENTSTREAM_INT32: {
        uint8_t *ptr = GGL_ALLOCN(alloc, uint8_t, 4);
        if (ptr == NULL) {
            GGL_LOGE(
                "eventstream", "Insufficent buffer space to encode packet."
            );
            return GGL_ERR_NOMEM;
        }
        uint32_t val;
        memcpy(&val, &header.value.int32, 4);
        write_be_u32(val, ptr);
        break;
    }
    case EVENTSTREAM_STRING: {
        GglBuffer str = header.value.string;

        if (str.len > UINT16_MAX) {
            GGL_LOGE(
                "eventstream", "String length exceeds eventstream limits."
            );
            return GGL_ERR_RANGE;
        }
        uint16_t str_len = (uint16_t) str.len;

        uint8_t *ptr = GGL_ALLOCN(alloc, uint8_t, 2);
        if (ptr == NULL) {
            GGL_LOGE(
                "eventstream", "Insufficent buffer space to encode packet."
            );
            return GGL_ERR_NOMEM;
        }
        ptr[0] = (uint8_t) (str_len >> 8);
        ptr[1] = (uint8_t) (str_len & 0xFF);

        ptr = GGL_ALLOCN(alloc, uint8_t, str_len);
        if (ptr == NULL) {
            GGL_LOGE(
                "eventstream", "Insufficent buffer space to encode packet."
            );
            return GGL_ERR_NOMEM;
        }
        memcpy(ptr, str.data, str_len);
        break;
    }
    default:
        GGL_LOGE("eventstream", "Unhandled header value type.");
        return GGL_ERR_PARSE;
    }

    return GGL_ERR_OK;
}

GglError eventstream_encode(
    GglBuffer *buf,
    const EventStreamHeader *headers,
    size_t header_count,
    GglError (*payload_writer)(GglBuffer *buf, void *payload),
    void *payload
) {
    if (buf->len > UINT32_MAX) {
        buf->len = UINT32_MAX;
    }

    GglBuffer buf_copy = *buf;

    if (buf_copy.len < 12) {
        GGL_LOGE("eventstream", "Insufficent buffer space to encode packet.");
        return GGL_ERR_NOMEM;
    }
    uint8_t *prelude = buf_copy.data;
    buf_copy = ggl_buffer_substr(buf_copy, 12, SIZE_MAX);

    uint8_t *message_len_p = prelude;
    uint8_t *headers_len_p = &prelude[4];
    uint8_t *prelude_crc_p = &prelude[8];

    uint32_t headers_len;

    {
        GglBumpAlloc bump_alloc = ggl_bump_alloc_init(buf_copy);

        for (size_t i = 0; i < header_count; i++) {
            GglError err = header_encode(&bump_alloc.alloc, headers[i]);
            if (err != GGL_ERR_OK) {
                return err;
            }
        }

        headers_len = (uint32_t) bump_alloc.index;
        buf_copy = ggl_buffer_substr(buf_copy, headers_len, SIZE_MAX);
    }

    write_be_u32(headers_len, headers_len_p);

    GglBuffer payload_buf = buf_copy;
    if (payload_writer == NULL) {
        payload_buf.len = 0;
    } else {
        GglError err = payload_writer(&payload_buf, payload);
        if (err != GGL_ERR_OK) {
            return err;
        }
        buf_copy = ggl_buffer_substr(buf_copy, payload_buf.len, SIZE_MAX);
    }

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
        GGL_LOGE("eventstream", "Insufficent buffer space to encode packet.");
        return GGL_ERR_NOMEM;
    }
    uint8_t *message_crc_p = buf_copy.data;

    write_be_u32(message_crc, message_crc_p);

    buf->len = message_len;

    return GGL_ERR_OK;
}
