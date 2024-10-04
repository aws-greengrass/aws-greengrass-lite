// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/buffer.h"
#include "ggl/alloc.h"
#include "ggl/error.h"
#include "ggl/log.h"
#include "ggl/object.h"
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

GglBuffer ggl_buffer_from_null_term(char *str) {
    return (GglBuffer) { .data = (uint8_t *) str, .len = strlen(str) };
}

bool ggl_buffer_eq(GglBuffer buf1, GglBuffer buf2) {
    if (buf1.len == buf2.len) {
        return memcmp(buf1.data, buf2.data, buf1.len) == 0;
    }
    return false;
}

bool ggl_buffer_has_prefix(GglBuffer buf, GglBuffer prefix) {
    if (prefix.len <= buf.len) {
        return memcmp(buf.data, prefix.data, prefix.len) == 0;
    }
    return false;
}

bool ggl_buffer_has_suffix(GglBuffer buf, GglBuffer suffix) {
    if (suffix.len <= buf.len) {
        return memcmp(&buf.data[buf.len - suffix.len], suffix.data, suffix.len)
            == 0;
    }
    return false;
}

bool ggl_buffer_contains(GglBuffer buf, GglBuffer substring, size_t *start) {
    if (substring.len == 0) {
        *start = 0;
        return true;
    }
    uint8_t *ptr = memmem(buf.data, buf.len, substring.data, substring.len);
    if (ptr == NULL) {
        return false;
    }
    *start = (size_t) (ptr - buf.data);
    return true;
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

GglError ggl_buf_clone(GglBuffer buf, GglAlloc *alloc, GglBuffer *out) {
    if (buf.len == 0) {
        *out = (GglBuffer) { 0 };
        return GGL_ERR_OK;
    }
    uint8_t *new_mem = GGL_ALLOCN(alloc, uint8_t, buf.len);
    if (new_mem == NULL) {
        GGL_LOGE(
            "buffer", "Insufficient memory when cloning buffer into %p.", alloc
        );
        return GGL_ERR_NOMEM;
    }
    memcpy(new_mem, buf.data, buf.len);
    out->data = new_mem;
    out->len = buf.len;
    return GGL_ERR_OK;
}
