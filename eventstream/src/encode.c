/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ggl/eventstream/encode.h"
#include "crc32.h"
#include "ggl/eventstream/types.h"
#include <assert.h>
#include <ggl/alloc.h>
#include <ggl/bump_alloc.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <string.h>
#include <stdint.h>

static void write_be_u32(uint32_t val, uint8_t *dest) {
    dest[0] = val >> 24;
    dest[1] = (val >> 16) & 0xFF;
    dest[2] = (val >> 8) & 0xFF;
    dest[3] = val & 0xFF;
}

static GglError header_encode(GglAlloc *alloc, EventStreamHeader header) {
    assert(alloc != NULL);

    uint8_t *header_value_type_p = GGL_ALLOCN(alloc, uint8_t, 1);

    if (header_value_type_p == NULL) {
        return GGL_ERR_NOMEM;
    }

    *header_value_type_p = (uint8_t) header.value.type;

    switch (header.value.type) {
    case EVENTSTREAM_INT32: {
        uint8_t *ptr = GGL_ALLOCN(alloc, uint8_t, 4);
        uint32_t val;
        memcpy(&val, &header.value.int32, 4);
        write_be_u32(val, ptr);
        break;
    }
    case EVENTSTREAM_STRING: {
        GglBuffer str = header.value.string;

        if (str.len > UINT16_MAX) {
            return GGL_ERR_RANGE;
        }
        uint16_t str_len = (uint16_t) str.len;

        uint8_t *ptr = GGL_ALLOCN(alloc, uint8_t, 2);
        ptr[0] = str_len >> 8;
        ptr[1] = str_len & 0xFF;

        ptr = GGL_ALLOCN(alloc, uint8_t, str_len);
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
    GglBuffer payload
) {
    if (buf->len > UINT32_MAX) {
        buf->len = UINT32_MAX;
    }

    GglBumpAlloc bump_alloc = ggl_bump_alloc_init(*buf);
    GglAlloc *alloc = &bump_alloc.alloc;

    uint8_t *prelude = GGL_ALLOCN(alloc, uint8_t, 12);
    if (prelude == NULL) {
        return GGL_ERR_NOMEM;
    }

    uint8_t *message_len_p = prelude;
    uint8_t *headers_len_p = &prelude[4];
    uint8_t *prelude_crc_p = &prelude[8];

    for (size_t i = 0; i < header_count; i++) {
        GglError err = header_encode(alloc, headers[i]);
        if (err != GGL_ERR_OK) {
            return err;
        }
    }

    uint32_t headers_len = (uint32_t) bump_alloc.index - 12;

    write_be_u32(headers_len, headers_len_p);

    uint8_t *payload_p = GGL_ALLOCN(alloc, uint8_t, payload.len);
    if (payload_p == NULL) {
        return GGL_ERR_NOMEM;
    }

    memcpy(payload_p, payload.data, payload.len);

    uint32_t message_len = 12 + headers_len + (uint32_t) payload.len + 4;

    write_be_u32(message_len, message_len_p);

    uint32_t prelude_crc
        = ggl_update_crc(0, (GglBuffer) { .data = prelude, .len = 8 });

    write_be_u32(prelude_crc, prelude_crc_p);

    uint32_t message_crc = ggl_update_crc(
        prelude_crc,
        (GglBuffer) { .data = &prelude[8], .len = bump_alloc.index - 8 }
    );

    uint8_t *message_crc_p = GGL_ALLOCN(alloc, uint8_t, 4);
    if (message_crc_p == NULL) {
        return GGL_ERR_NOMEM;
    }

    write_be_u32(message_crc, message_crc_p);

    buf->len = bump_alloc.index;

    return GGL_ERR_OK;
}
