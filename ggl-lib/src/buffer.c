// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/buffer.h"
#include "ggl/error.h"
#include "ggl/log.h"
#include "ggl/object.h"
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool ggl_buffer_eq(GglBuffer buf1, GglBuffer buf2) {
    if (buf1.len == buf2.len) {
        return memcmp(buf1.data, buf2.data, buf1.len) == 0;
    }
    return false;
}

GglBuffer ggl_buffer_substr(GglBuffer buf, size_t start, size_t end) {
    size_t start_trunc = start < buf.len ? start : buf.len;
    size_t end_trunc = end < buf.len ? end : buf.len;
    return (GglBuffer) {
        .data = &buf.data[start_trunc],
        .len = end_trunc >= start_trunc ? end_trunc - start_trunc : 0U,
    };
}

static bool mult_overflow_int64(int64_t a, int64_t b) {
    if (b == 0) {
        return false;
    }
    return b > 0 ? ((a > INT64_MAX / b) || (a < INT64_MIN / b))
                 : ((a < INT64_MAX / b) || (a > INT64_MIN / b));
}

static bool add_overflow_int64(int64_t a, int64_t b) {
    return b > 0 ? (a > INT64_MAX - b) : (a < INT64_MIN - b);
}

GglError ggl_str_to_int64(GglBuffer str, int64_t *value) {
    int64_t ret = 0;
    int64_t sign = 1;
    size_t i = 0;

    if ((str.len >= 1) && (str.data[0] == '-')) {
        i = 1;
        sign = -1;
    }

    if (i == str.len) {
        GGL_LOGE("buffer", "Insufficient characters when parsing int64.");
        return GGL_ERR_INVALID;
    }

    for (; i < str.len; i++) {
        uint8_t c = str.data[i];

        if ((c < '0') || (c > '9')) {
            GGL_LOGE("buffer", "Invalid character %c when parsing int64.", c);
            return GGL_ERR_INVALID;
        }

        if (mult_overflow_int64(ret, 10U)) {
            GGL_LOGE("buffer", "Overflow when parsing int64 from buffer.");
            return GGL_ERR_RANGE;
        }
        ret *= 10;

        if (add_overflow_int64(ret, sign * (c - '0'))) {
            GGL_LOGE("buffer", "Overflow when parsing int64 from buffer.");
            return GGL_ERR_RANGE;
        }
        ret += sign * (c - '0');
    }

    *value = ret;
    return GGL_ERR_OK;
}
