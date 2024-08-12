// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/base64.h"
#include "ggl/alloc.h"
#include "ggl/buffer.h"
#include "ggl/error.h"
#include "ggl/object.h"
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

static bool base64_char_to_byte(char digit, uint8_t *value) {
    if ((digit >= 'A') && (digit <= 'Z')) {
        *value = (uint8_t) (digit - 'A');
    } else if ((digit >= 'a') && (digit <= 'z')) {
        *value = (uint8_t) (digit - 'a' + ('Z' - 'A' + 1));
    } else if ((digit >= '0') && (digit <= '9')) {
        *value = (uint8_t) (digit - '0' + ('Z' - 'A' + 1) + ('z' - 'a' + 1));
    } else if (digit == '+') {
        *value = 62;
    } else if (digit == '/') {
        *value = 63;
    } else {
        return false;
    }
    return true;
}

static bool base64_decode_segment(
    const uint8_t segment[4U], GglBuffer *target
) {
    uint8_t value[3U] = { 0 };
    size_t len = 0U;

    uint8_t decoded = 0U;
    bool ret = base64_char_to_byte((char) segment[0U], &decoded);
    if (!ret) {
        return false;
    }

    value[0U] = (uint8_t) (decoded << 2U);

    ret = base64_char_to_byte((char) segment[1U], &decoded);
    if (!ret) {
        return false;
    }

    value[0U] |= (uint8_t) (decoded >> 4U);
    value[1U] = (uint8_t) (decoded << 4U);

    if (segment[2U] == '=') {
        if (segment[3U] != '=') {
            // non-padding byte after padding
            return false;
        }
        if (value[1U] != 0U) {
            // bad encoding (includes unused bits)
            return false;
        }
        len = 1U;
    } else {
        ret = base64_char_to_byte((char) segment[2U], &decoded);
        if (!ret) {
            return false;
        }

        value[1U] |= (uint8_t) (decoded >> 2U);
        value[2U] = (uint8_t) (decoded << 6U);

        if (segment[3U] == '=') {
            if (value[2U] != 0U) {
                // bad encoding (includes unused bits)
                return false;
            }
            len = 2U;
        } else {
            ret = base64_char_to_byte((char) segment[3U], &decoded);
            if (!ret) {
                return false;
            }

            value[2U] |= decoded;
            len = 3U;
        }
    }

    if (len >= target->len) {
        return false;
    }

    memcpy(target->data, value, len);
    *target = ggl_buffer_substr(*target, len, SIZE_MAX);

    return true;
}

bool ggl_base64_decode(GglBuffer base64, GglBuffer *target) {
    if ((base64.len % 4) != 0) {
        return false;
    }
    if (target->len < ((base64.len / 4) * 3)) {
        return false;
    }
    GglBuffer out = *target;
    for (size_t i = 0; i < base64.len; i += 4) {
        bool ret = base64_decode_segment(&base64.data[i], &out);
        if (!ret) {
            return false;
        }
    }
    target->len = (size_t) (out.data - target->data);
    return true;
}

bool ggl_base64_decode_in_place(GglBuffer *target) {
    return ggl_base64_decode(*target, target);
}

static const uint8_t BASE64_TABLE[]
    = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

GglError ggl_base64_encode(GglBuffer buf, GglAlloc *alloc, GglBuffer *result) {
    size_t base64_len = ((buf.len + 2) / 3) * 4;
    uint8_t *mem = GGL_ALLOCN(alloc, uint8_t, base64_len);
    if (mem == NULL) {
        return GGL_ERR_NOMEM;
    }

    for (size_t i = 0, j = 0; i < buf.len;) {
        uint32_t chunk = (unsigned) (i < buf.len ? buf.data[i++] : 0) << 16;
        chunk += (unsigned) (i < buf.len ? buf.data[i++] : 0) << 8;
        chunk += (unsigned) (i < buf.len ? buf.data[i++] : 0);

        mem[j++] = BASE64_TABLE[chunk >> 18];
        mem[j++] = BASE64_TABLE[(chunk >> 12) & 0x3F];
        mem[j++] = BASE64_TABLE[(chunk >> 6) & 0x3F];
        mem[j++] = BASE64_TABLE[chunk & 0x3F];
    }

    *result = (GglBuffer) { .data = mem, .len = base64_len };
    return GGL_ERR_OK;
}
