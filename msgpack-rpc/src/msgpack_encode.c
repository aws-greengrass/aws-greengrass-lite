/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "./msgpack.h"
#include "ggl/alloc.h"
#include "ggl/bump_alloc.h"
#include "ggl/error.h"
#include "ggl/log.h"
#include "ggl/object.h"
#include <assert.h>
#include <endian.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

static GglError write_obj(GglAlloc *alloc, GglObject obj);

static GglError write_null(GglAlloc *alloc) {
    assert(alloc != NULL);
    uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, 1);
    if (buf == NULL) {
        return GGL_ERR_NOMEM;
    }
    buf[0] = 0xC0;
    return 0;
}

static GglError write_bool(GglAlloc *alloc, bool boolean) {
    assert(alloc != NULL);
    uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, 1);
    if (buf == NULL) {
        return GGL_ERR_NOMEM;
    }
    buf[0] = boolean ? 0xC3 : 0xC2;
    return 0;
}

static GglError write_u64(GglAlloc *alloc, uint64_t u64) {
    assert(alloc != NULL);
    uint64_t u64be = htobe64(u64);
    if (u64 <= UINT8_MAX) {
        uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, 2);
        if (buf == NULL) {
            return GGL_ERR_NOMEM;
        }
        buf[0] = 0xCC;
        memcpy(&buf[1], &((char *) &u64be)[7], 1);
    } else if (u64 <= UINT16_MAX) {
        uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, 3);
        if (buf == NULL) {
            return GGL_ERR_NOMEM;
        }
        buf[0] = 0xCD;
        memcpy(&buf[1], &((char *) &u64be)[6], 2);
    } else if (u64 <= UINT32_MAX) {
        uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, 5);
        if (buf == NULL) {
            return GGL_ERR_NOMEM;
        }
        buf[0] = 0xCE;
        memcpy(&buf[1], &((char *) &u64be)[4], 4);
    } else {
        uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, 9);
        if (buf == NULL) {
            return GGL_ERR_NOMEM;
        }
        buf[0] = 0xCF;
        memcpy(&buf[1], &u64be, 8);
    }
    return 0;
}

/** Checks if twos-complement representation fits in less bytes. */
static bool i64_fits_bytes(uint64_t ti64, uint8_t bytes) {
    assert(bytes <= 7);
    // get mask for sign bit and sign-extended bits, avoiding integer promotion
    uint64_t sign_mask = UINT64_MAX << ((unsigned int) bytes * 8U - 1U);
    uint64_t sign_bits = ti64 & sign_mask;
    return (sign_bits == 0U) || ((sign_bits | ~sign_mask) == UINT64_MAX);
}

static GglError write_i64(GglAlloc *alloc, int64_t i64) {
    static_assert((uint32_t) -1 == 0xFFFFFFFFUL, "twos-compliment required");
    assert(alloc != NULL);

    if ((i64 >= 0) && (i64 <= 0x7F)) {
        // positive fixint
        uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, 1);
        if (buf == NULL) {
            return GGL_ERR_NOMEM;
        }
        buf[0] = (uint8_t) i64;
        return 0;
    }
    if ((i64 < 0) && (i64 >= -0x1F)) {
        // negative fixint
        uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, 1);
        if (buf == NULL) {
            return GGL_ERR_NOMEM;
        }
        int8_t val = (int8_t) i64;
        memcpy(buf, &val, 1);
        return 0;
    }

    if (i64 >= 0) {
        return write_u64(alloc, (uint64_t) i64);
    }

    uint64_t i32b;
    memcpy(&i32b, &i64, sizeof(int64_t));
    uint64_t i32bbe = htobe64(i32b);

    if (i64_fits_bytes(i32b, 1)) {
        uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, 2);
        if (buf == NULL) {
            return GGL_ERR_NOMEM;
        }
        buf[0] = 0xD0;
        memcpy(&buf[1], &((char *) &i32bbe)[7], 1);
    } else if (i64_fits_bytes(i32b, 2)) {
        uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, 3);
        if (buf == NULL) {
            return GGL_ERR_NOMEM;
        }
        buf[0] = 0xD1;
        memcpy(&buf[1], &((char *) &i32bbe)[6], 2);
    } else if (i64_fits_bytes(i32b, 4)) {
        uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, 5);
        if (buf == NULL) {
            return GGL_ERR_NOMEM;
        }
        buf[0] = 0xD2;
        memcpy(&buf[1], &((char *) &i32bbe)[4], 4);
    } else {
        uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, 9);
        if (buf == NULL) {
            return GGL_ERR_NOMEM;
        }
        buf[0] = 0xD3;
        memcpy(&buf[1], &i32bbe, 8);
    }
    return 0;
}

static GglError write_f64(GglAlloc *alloc, double f64) {
    assert(alloc != NULL);
    float f32 = (float) f64;
    if (f64 == (double) f32) {
        // No precision loss, encode as f32
        uint32_t f32_bytes;
        // memcpy necessary for well-defined type-punning
        static_assert(sizeof(float) == sizeof(int32_t), "float is not 32 bits");
        memcpy(&f32_bytes, &f32, sizeof(int32_t));
        f32_bytes = htobe32(f32_bytes);

        uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, 5);
        if (buf == NULL) {
            return GGL_ERR_NOMEM;
        }
        buf[0] = 0xCA;
        memcpy(&buf[1], &f32_bytes, 4);
    } else {
        uint64_t f64_bytes;
        static_assert(
            sizeof(double) == sizeof(int64_t), "double is not 64 bits"
        );
        memcpy(&f64_bytes, &f64, sizeof(int64_t));
        f64_bytes = htobe64(f64_bytes);

        uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, 9);
        if (buf == NULL) {
            return GGL_ERR_NOMEM;
        }
        buf[0] = 0xCB;
        memcpy(&buf[1], &f64_bytes, 8);
    }
    return 0;
}

static GglError write_str(GglAlloc *alloc, GglBuffer str) {
    assert(alloc != NULL);
    if (str.len < 32) {
        // fixstr
        uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, 1);
        if (buf == NULL) {
            return GGL_ERR_NOMEM;
        }
        buf[0] = 0xA0 | (uint8_t) str.len;
    } else if (str.len < UINT8_MAX) {
        uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, 2);
        if (buf == NULL) {
            return GGL_ERR_NOMEM;
        }
        buf[0] = 0xD9;
        buf[1] = (uint8_t) str.len;
    } else if (str.len < UINT16_MAX) {
        uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, 3);
        if (buf == NULL) {
            return GGL_ERR_NOMEM;
        }
        buf[0] = 0xDA;
        uint16_t bytes = htobe16((uint16_t) str.len);
        memcpy(&buf[1], &bytes, 2);
    } else if (str.len < UINT32_MAX) {
        uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, 5);
        if (buf == NULL) {
            return GGL_ERR_NOMEM;
        }
        buf[0] = 0xDB;
        uint32_t bytes = htobe32((uint32_t) str.len);
        memcpy(&buf[1], &bytes, 4);
    } else {
        GGL_LOGE("msgpack", "Can't encode str of len %zu.", str.len);
        return GGL_ERR_RANGE;
    }
    uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, str.len);
    if (buf == NULL) {
        return GGL_ERR_NOMEM;
    }
    memcpy(buf, str.data, str.len);
    return 0;
}

static GglError write_buf(GglAlloc *alloc, GglBuffer buffer) {
    assert(alloc != NULL);
    if (buffer.len < UINT8_MAX) {
        uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, 2);
        if (buf == NULL) {
            return GGL_ERR_NOMEM;
        }
        buf[0] = 0xC4;
        buf[1] = (uint8_t) buffer.len;
    } else if (buffer.len < UINT16_MAX) {
        uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, 3);
        if (buf == NULL) {
            return GGL_ERR_NOMEM;
        }
        buf[0] = 0xC5;
        uint16_t bytes = htobe16((uint16_t) buffer.len);
        memcpy(&buf[1], &bytes, 2);
    } else if (buffer.len < UINT32_MAX) {
        uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, 5);
        if (buf == NULL) {
            return GGL_ERR_NOMEM;
        }
        buf[0] = 0xC6;
        uint32_t bytes = htobe32((uint32_t) buffer.len);
        memcpy(&buf[1], &bytes, 4);
    } else {
        GGL_LOGE("msgpack", "Can't encode buffer of len %zu.", buffer.len);
        return GGL_ERR_RANGE;
    }
    uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, buffer.len);
    if (buf == NULL) {
        return GGL_ERR_NOMEM;
    }
    memcpy(buf, buffer.data, buffer.len);
    return 0;
}

// NOLINTNEXTLINE(misc-no-recursion)
static GglError write_list(GglAlloc *alloc, GglList list) {
    assert(alloc != NULL);
    if (list.len < 16) {
        // fixarray
        uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, 1);
        if (buf == NULL) {
            return GGL_ERR_NOMEM;
        }
        buf[0] = 0x90 | (uint8_t) list.len;
    } else if (list.len < UINT16_MAX) {
        uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, 3);
        if (buf == NULL) {
            return GGL_ERR_NOMEM;
        }
        buf[0] = 0xDC;
        uint16_t bytes = htobe16((uint16_t) list.len);
        memcpy(&buf[1], &bytes, 2);
    } else if (list.len < UINT32_MAX) {
        uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, 5);
        if (buf == NULL) {
            return GGL_ERR_NOMEM;
        }
        buf[0] = 0xDD;
        uint32_t bytes = htobe32((uint32_t) list.len);
        memcpy(&buf[1], &bytes, 4);
    } else {
        GGL_LOGE("msgpack", "Can't encode list of len %zu.", list.len);
        return GGL_ERR_RANGE;
    }

    for (size_t i = 0; i < list.len; i++) {
        GglError ret = write_obj(alloc, list.items[i]);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }
    return 0;
}

// NOLINTNEXTLINE(misc-no-recursion)
static GglError write_map(GglAlloc *alloc, GglMap map) {
    assert(alloc != NULL);
    if (map.len < 16) {
        // fixmap
        uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, 1);
        if (buf == NULL) {
            return GGL_ERR_NOMEM;
        }
        buf[0] = 0x80 | (uint8_t) map.len;
    } else if (map.len < UINT16_MAX) {
        uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, 3);
        if (buf == NULL) {
            return GGL_ERR_NOMEM;
        }
        buf[0] = 0xDC;
        uint16_t bytes = htobe16((uint16_t) map.len);
        memcpy(&buf[1], &bytes, 2);
    } else if (map.len < UINT32_MAX) {
        uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, 5);
        if (buf == NULL) {
            return GGL_ERR_NOMEM;
        }
        buf[0] = 0xDD;
        uint32_t bytes = htobe32((uint32_t) map.len);
        memcpy(&buf[1], &bytes, 4);
    } else {
        GGL_LOGE("msgpack", "Can't encode map of len %zu.", map.len);
        return GGL_ERR_RANGE;
    }

    for (size_t i = 0; i < map.len; i++) {
        GglKV pair = map.pairs[i];
        GglError ret = write_str(alloc, pair.key);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        ret = write_obj(alloc, pair.val);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }
    return 0;
}

// NOLINTNEXTLINE(misc-no-recursion)
static GglError write_obj(GglAlloc *alloc, GglObject obj) {
    assert(alloc != NULL);
    switch (obj.type) {
    case GGL_TYPE_NULL: return write_null(alloc);
    case GGL_TYPE_BOOLEAN: return write_bool(alloc, obj.boolean);
    case GGL_TYPE_I64: return write_i64(alloc, obj.i64);
    case GGL_TYPE_F64: return write_f64(alloc, obj.f64);
    case GGL_TYPE_BUF: return write_buf(alloc, obj.buf);
    case GGL_TYPE_LIST: return write_list(alloc, obj.list);
    case GGL_TYPE_MAP: return write_map(alloc, obj.map);
    }
    return GGL_ERR_INVALID;
}

GglError ggl_msgpack_encode(GglObject obj, GglBuffer *buf) {
    assert((buf != NULL) && (buf->data != NULL));
    GglBumpAlloc mem = ggl_bump_alloc_init(*buf);
    GglError ret = write_obj(&mem.alloc, obj);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    buf->len = mem.index;
    return 0;
}
