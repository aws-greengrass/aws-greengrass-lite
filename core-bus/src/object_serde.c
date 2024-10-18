// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "object_serde.h"
#include <assert.h>
#include <ggl/alloc.h>
#include <ggl/bump_alloc.h>
#include <ggl/constants.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

static_assert(
    __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "host endian not supported"
);

typedef struct {
    enum {
        HANDLING_OBJ,
        HANDLING_KV
    } type;

    union {
        GglObject *obj_next;
        GglKV *kv_next;
    };

    uint32_t remaining;
} NestingLevel;

typedef struct {
    NestingLevel levels[GGL_MAX_OBJECT_DEPTH];
    size_t level;
} NestingState;

static GglError push_parse_state(NestingState *state, NestingLevel level) {
    if (state->level >= GGL_MAX_OBJECT_DEPTH) {
        GGL_LOGE("Packet object exceeded max nesting depth.");
        return GGL_ERR_RANGE;
    }

    state->level += 1;
    state->levels[state->level - 1] = level;
    return GGL_ERR_OK;
}

static GglError buf_take(size_t n, GglBuffer *buf, GglBuffer *out) {
    assert((buf != NULL) && (buf->data != NULL) && (out != NULL));

    if (n > buf->len) {
        GGL_LOGE("Packet decode exceeded bounds.");
        return GGL_ERR_PARSE;
    }

    *out = (GglBuffer) { .len = n, .data = buf->data };
    buf->len -= n;
    buf->data = &buf->data[n];
    return GGL_ERR_OK;
}

static GglError write_bool(GglAlloc *alloc, bool boolean) {
    assert(alloc != NULL);

    uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, 1);
    if (buf == NULL) {
        GGL_LOGE("Insufficient memory to encode packet.");
        return GGL_ERR_NOMEM;
    }

    buf[0] = boolean;
    return GGL_ERR_OK;
}

static GglError read_bool(GglBuffer *buf, GglObject *obj) {
    GglBuffer temp_buf;
    GglError ret = buf_take(1, buf, &temp_buf);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    *obj = GGL_OBJ_BOOL(temp_buf.data[0]);
    return GGL_ERR_OK;
}

static GglError write_i64(GglAlloc *alloc, int64_t i64) {
    assert(alloc != NULL);

    uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, sizeof(int64_t));
    if (buf == NULL) {
        GGL_LOGE("Insufficient memory to encode packet.");
        return GGL_ERR_NOMEM;
    }

    memcpy(buf, &i64, sizeof(int64_t));
    return GGL_ERR_OK;
}

static GglError read_i64(GglBuffer *buf, GglObject *obj) {
    GglBuffer temp_buf;
    GglError ret = buf_take(sizeof(int64_t), buf, &temp_buf);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    int64_t val;
    memcpy(&val, temp_buf.data, sizeof(int64_t));
    *obj = GGL_OBJ_I64(val);
    return GGL_ERR_OK;
}

static GglError write_f64(GglAlloc *alloc, double f64) {
    assert(alloc != NULL);

    uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, sizeof(double));
    if (buf == NULL) {
        GGL_LOGE("Insufficient memory to encode packet.");
        return GGL_ERR_NOMEM;
    }

    memcpy(buf, &f64, sizeof(double));
    return GGL_ERR_OK;
}

static GglError read_f64(GglBuffer *buf, GglObject *obj) {
    GglBuffer temp_buf;
    GglError ret = buf_take(sizeof(double), buf, &temp_buf);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    double val;
    memcpy(&val, temp_buf.data, sizeof(double));
    *obj = GGL_OBJ_F64(val);
    return GGL_ERR_OK;
}

static GglError write_buf(GglAlloc *alloc, GglBuffer buffer) {
    assert(alloc != NULL);

    if (buffer.len > UINT32_MAX) {
        GGL_LOGE("Can't encode buffer of len %zu.", buffer.len);
        return GGL_ERR_RANGE;
    }
    uint32_t len = (uint32_t) buffer.len;

    uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, sizeof(len) + buffer.len);
    if (buf == NULL) {
        GGL_LOGE("Insufficient memory to encode packet.");
        return GGL_ERR_NOMEM;
    }

    memcpy(buf, &len, sizeof(len));
    memcpy(&buf[sizeof(len)], buffer.data, len);
    return GGL_ERR_OK;
}

static GglError read_buf_raw(
    GglAlloc *alloc, bool copy_bufs, GglBuffer *buf, GglBuffer *out
) {
    GglBuffer temp_buf;
    uint32_t len;
    GglError ret = buf_take(sizeof(len), buf, &temp_buf);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    memcpy(&len, temp_buf.data, sizeof(len));

    ret = buf_take(len, buf, &temp_buf);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (copy_bufs) {
        uint8_t *copy = GGL_ALLOCN(alloc, uint8_t, len);
        if (copy == NULL) {
            GGL_LOGE("Insufficient memory to encode packet.");
            return GGL_ERR_NOMEM;
        }

        memcpy(copy, temp_buf.data, len);
        temp_buf.data = copy;
    }

    *out = temp_buf;
    return GGL_ERR_OK;
}

static GglError read_buf(
    GglAlloc *alloc, bool copy_bufs, GglBuffer *buf, GglObject *obj
) {
    GglBuffer val;
    GglError ret = read_buf_raw(alloc, copy_bufs, buf, &val);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    *obj = GGL_OBJ_BUF(val);
    return GGL_ERR_OK;
}

static GglError write_list(GglAlloc *alloc, NestingState *state, GglList list) {
    assert(alloc != NULL);

    if (list.len > UINT32_MAX) {
        GGL_LOGE("Can't encode list of len %zu.", list.len);
        return GGL_ERR_RANGE;
    }
    uint32_t len = (uint32_t) list.len;

    uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, sizeof(len));
    if (buf == NULL) {
        GGL_LOGE("Insufficient memory to encode packet.");
        return GGL_ERR_NOMEM;
    }

    memcpy(buf, &len, sizeof(len));

    GglError ret = push_parse_state(
        state,
        (NestingLevel) {
            .type = HANDLING_OBJ,
            .obj_next = list.items,
            .remaining = len,
        }
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return GGL_ERR_OK;
}

static GglError read_list(
    GglAlloc *alloc, NestingState *state, GglBuffer *buf, GglObject *obj
) {
    GglBuffer temp_buf;
    uint32_t len;
    GglError ret = buf_take(sizeof(len), buf, &temp_buf);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    memcpy(&len, temp_buf.data, sizeof(len));

    GglList val = { .len = len };

    if (len > 0) {
        if (alloc == NULL) {
            GGL_LOGE("Packet decode requires allocation and no alloc provided."
            );
            return GGL_ERR_NOMEM;
        }

        val.items = GGL_ALLOCN(alloc, GglObject, len);
        if (val.items == NULL) {
            GGL_LOGE("Insufficient memory to decode packet.");
            return GGL_ERR_NOMEM;
        }

        ret = push_parse_state(
            state,
            (NestingLevel) {
                .type = HANDLING_OBJ,
                .obj_next = val.items,
                .remaining = len,
            }
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    *obj = GGL_OBJ_LIST(val);
    return GGL_ERR_OK;
}

static GglError write_map(GglAlloc *alloc, NestingState *state, GglMap map) {
    assert(alloc != NULL);

    if (map.len > UINT32_MAX) {
        GGL_LOGE("Can't encode map of len %zu.", map.len);
        return GGL_ERR_RANGE;
    }
    uint32_t len = (uint32_t) map.len;

    uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, sizeof(len));
    if (buf == NULL) {
        GGL_LOGE("Insufficient memory to encode packet.");
        return GGL_ERR_NOMEM;
    }

    memcpy(buf, &len, sizeof(len));

    GglError ret = push_parse_state(
        state,
        (NestingLevel) {
            .type = HANDLING_KV,
            .kv_next = map.pairs,
            .remaining = len,
        }
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    return GGL_ERR_OK;
}

static GglError read_map(
    GglAlloc *alloc, NestingState *state, GglBuffer *buf, GglObject *obj
) {
    GglBuffer temp_buf;
    uint32_t len;
    GglError ret = buf_take(sizeof(len), buf, &temp_buf);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    memcpy(&len, temp_buf.data, sizeof(len));

    GglMap val = { .len = len };

    if (len > 0) {
        if (alloc == NULL) {
            GGL_LOGE("Packet decode requires allocation and no alloc provided."
            );
            return GGL_ERR_NOMEM;
        }

        val.pairs = GGL_ALLOCN(alloc, GglKV, len);
        if (val.pairs == NULL) {
            GGL_LOGE("Insufficient memory to decode packet.");
            return GGL_ERR_NOMEM;
        }

        ret = push_parse_state(
            state,
            (NestingLevel) {
                .type = HANDLING_KV,
                .kv_next = val.pairs,
                .remaining = len,
            }
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    *obj = GGL_OBJ_MAP(val);
    return GGL_ERR_OK;
}

static GglError write_obj(GglAlloc *alloc, NestingState *state, GglObject obj) {
    uint8_t *buf = GGL_ALLOCN(alloc, uint8_t, 1);
    if (buf == NULL) {
        GGL_LOGE("Insufficient memory to encode packet.");
        return GGL_ERR_NOMEM;
    }
    buf[0] = (uint8_t) obj.type;

    assert(alloc != NULL);
    switch (obj.type) {
    case GGL_TYPE_NULL:
        return GGL_ERR_OK;
    case GGL_TYPE_BOOLEAN:
        return write_bool(alloc, obj.boolean);
    case GGL_TYPE_I64:
        return write_i64(alloc, obj.i64);
    case GGL_TYPE_F64:
        return write_f64(alloc, obj.f64);
    case GGL_TYPE_BUF:
        return write_buf(alloc, obj.buf);
    case GGL_TYPE_LIST:
        return write_list(alloc, state, obj.list);
    case GGL_TYPE_MAP:
        return write_map(alloc, state, obj.map);
    }
    return GGL_ERR_INVALID;
}

static GglError read_obj(
    GglAlloc *alloc,
    bool copy_bufs,
    NestingState *state,
    GglBuffer *buf,
    GglObject *obj
) {
    assert((buf != NULL) && (obj != NULL));

    GglBuffer temp_buf;
    GglError ret = buf_take(1, buf, &temp_buf);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    uint8_t tag = temp_buf.data[0];

    switch (tag) {
    case GGL_TYPE_NULL:
        *obj = GGL_OBJ_NULL();
        return GGL_ERR_OK;
    case GGL_TYPE_BOOLEAN:
        return read_bool(buf, obj);
    case GGL_TYPE_I64:
        return read_i64(buf, obj);
    case GGL_TYPE_F64:
        return read_f64(buf, obj);
    case GGL_TYPE_BUF:
        return read_buf(alloc, copy_bufs, buf, obj);
    case GGL_TYPE_LIST:
        return read_list(alloc, state, buf, obj);
    case GGL_TYPE_MAP:
        return read_map(alloc, state, buf, obj);
    default:
        break;
    }
    return GGL_ERR_INVALID;
}

GglError ggl_serialize(GglObject obj, GglBuffer *buf) {
    assert((buf != NULL) && (buf->data != NULL));
    GglBumpAlloc mem = ggl_bump_alloc_init(*buf);

    NestingState state = {
        .levels = { {
            .type = HANDLING_OBJ,
            .obj_next = &obj,
            .remaining = 1,
        } },
        .level = 1,
    };

    do {
        NestingLevel *level = &state.levels[state.level - 1];

        if (level->remaining == 0) {
            state.level -= 1;
            continue;
        }

        if (level->type == HANDLING_OBJ) {
            GglError ret = write_obj(&mem.alloc, &state, *level->obj_next);
            if (ret != GGL_ERR_OK) {
                return ret;
            }
            level->remaining -= 1;
            level->obj_next = &level->obj_next[1];
        } else if (level->type == HANDLING_KV) {
            GglError ret = write_buf(&mem.alloc, level->kv_next->key);
            if (ret != GGL_ERR_OK) {
                return ret;
            }
            ret = write_obj(&mem.alloc, &state, level->kv_next->val);
            if (ret != GGL_ERR_OK) {
                return ret;
            }
            level->remaining -= 1;
            level->kv_next = &level->kv_next[1];
        } else {
            assert(false);
        }
    } while (state.level > 0);

    buf->len = mem.index;
    return GGL_ERR_OK;
}

GglError ggl_deserialize(
    GglAlloc *alloc, bool copy_bufs, GglBuffer buf, GglObject *obj
) {
    assert(obj != NULL);

    GglBuffer rest = buf;

    NestingState state = {
        .levels = { {
            .type = HANDLING_OBJ,
            .obj_next = obj,
            .remaining = 1,
        } },
        .level = 1,
    };

    do {
        NestingLevel *level = &state.levels[state.level - 1];

        if (level->remaining == 0) {
            state.level -= 1;
            continue;
        }

        if (level->type == HANDLING_OBJ) {
            GglError ret
                = read_obj(alloc, copy_bufs, &state, &rest, level->obj_next);
            if (ret != GGL_ERR_OK) {
                return ret;
            }
            level->remaining -= 1;
            level->obj_next = &level->obj_next[1];
        } else if (level->type == HANDLING_KV) {
            GglError ret
                = read_buf_raw(alloc, copy_bufs, &rest, &level->kv_next->key);
            if (ret != GGL_ERR_OK) {
                return ret;
            }
            ret = read_obj(
                alloc, copy_bufs, &state, &rest, &level->kv_next->val
            );
            if (ret != GGL_ERR_OK) {
                return ret;
            }
            level->remaining -= 1;
            level->kv_next = &level->kv_next[1];
        } else {
            assert(false);
        }
    } while (state.level > 0);

    // Ensure no trailing data
    if (rest.len != 0) {
        GGL_LOGE("Payload has %zu trailing bytes.", rest.len);
        return GGL_ERR_PARSE;
    }

    return GGL_ERR_OK;
}
