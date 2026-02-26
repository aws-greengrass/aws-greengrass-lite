// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "object_serde.h"
#include <assert.h>
#include <gg/arena.h>
#include <gg/error.h>
#include <gg/io.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
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
        GgObject *obj_next;
        GgKV *kv_next;
    };

    uint32_t remaining;
} NestingLevel;

typedef struct {
    NestingLevel levels[GG_MAX_OBJECT_DEPTH];
    size_t level;
} NestingState;

static GgError push_parse_state(NestingState *state, NestingLevel level) {
    if (state->level >= GG_MAX_OBJECT_DEPTH) {
        GG_LOGE("Packet object exceeded max nesting depth.");
        return GG_ERR_RANGE;
    }

    state->level += 1;
    state->levels[state->level - 1] = level;
    return GG_ERR_OK;
}

static GgError buf_take(size_t n, GgBuffer *buf, GgBuffer *out) {
    assert((buf != NULL) && (buf->data != NULL) && (out != NULL));

    if (n > buf->len) {
        GG_LOGE("Packet decode exceeded bounds.");
        return GG_ERR_PARSE;
    }

    *out = (GgBuffer) { .len = n, .data = buf->data };
    buf->len -= n;
    buf->data = &buf->data[n];
    return GG_ERR_OK;
}

static GgError write_bool(GgArena *alloc, bool boolean) {
    assert(alloc != NULL);

    uint8_t *buf = GG_ARENA_ALLOCN(alloc, uint8_t, 1);
    if (buf == NULL) {
        GG_LOGE("Insufficient memory to encode packet.");
        return GG_ERR_NOMEM;
    }

    buf[0] = boolean;
    return GG_ERR_OK;
}

static GgError read_bool(GgBuffer *buf, GgObject *obj) {
    GgBuffer temp_buf;
    GgError ret = buf_take(1, buf, &temp_buf);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    *obj = gg_obj_bool(temp_buf.data[0]);
    return GG_ERR_OK;
}

static GgError write_i64(GgArena *alloc, int64_t i64) {
    assert(alloc != NULL);

    // NOLINTNEXTLINE(bugprone-sizeof-expression)
    uint8_t *buf = GG_ARENA_ALLOCN(alloc, uint8_t, sizeof(int64_t));
    if (buf == NULL) {
        GG_LOGE("Insufficient memory to encode packet.");
        return GG_ERR_NOMEM;
    }

    memcpy(buf, &i64, sizeof(int64_t));
    return GG_ERR_OK;
}

static GgError read_i64(GgBuffer *buf, GgObject *obj) {
    GgBuffer temp_buf;
    GgError ret = buf_take(sizeof(int64_t), buf, &temp_buf);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    int64_t val;
    memcpy(&val, temp_buf.data, sizeof(int64_t));
    *obj = gg_obj_i64(val);
    return GG_ERR_OK;
}

static GgError write_f64(GgArena *alloc, double f64) {
    assert(alloc != NULL);

    // NOLINTNEXTLINE(bugprone-sizeof-expression)
    uint8_t *buf = GG_ARENA_ALLOCN(alloc, uint8_t, sizeof(double));
    if (buf == NULL) {
        GG_LOGE("Insufficient memory to encode packet.");
        return GG_ERR_NOMEM;
    }

    memcpy(buf, &f64, sizeof(double));
    return GG_ERR_OK;
}

static GgError read_f64(GgBuffer *buf, GgObject *obj) {
    GgBuffer temp_buf;
    GgError ret = buf_take(sizeof(double), buf, &temp_buf);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    double val;
    memcpy(&val, temp_buf.data, sizeof(double));
    *obj = gg_obj_f64(val);
    return GG_ERR_OK;
}

static GgError write_buf(GgArena *alloc, GgBuffer buffer) {
    assert(alloc != NULL);

    if (buffer.len > UINT32_MAX) {
        GG_LOGE("Can't encode buffer of len %zu.", buffer.len);
        return GG_ERR_RANGE;
    }
    uint32_t len = (uint32_t) buffer.len;

    uint8_t *buf = GG_ARENA_ALLOCN(alloc, uint8_t, sizeof(len) + buffer.len);
    if (buf == NULL) {
        GG_LOGE("Insufficient memory to encode packet.");
        return GG_ERR_NOMEM;
    }

    memcpy(buf, &len, sizeof(len));
    memcpy(&buf[sizeof(len)], buffer.data, len);
    return GG_ERR_OK;
}

static GgError read_buf_raw(GgBuffer *buf, GgBuffer *out) {
    GgBuffer temp_buf;
    uint32_t len;
    GgError ret = buf_take(sizeof(len), buf, &temp_buf);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    memcpy(&len, temp_buf.data, sizeof(len));

    ret = buf_take(len, buf, &temp_buf);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    *out = temp_buf;
    return GG_ERR_OK;
}

static GgError read_buf(GgBuffer *buf, GgObject *obj) {
    GgBuffer val;
    GgError ret = read_buf_raw(buf, &val);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    *obj = gg_obj_buf(val);
    return GG_ERR_OK;
}

static GgError write_list(GgArena *alloc, NestingState *state, GgList list) {
    assert(alloc != NULL);

    if (list.len > UINT32_MAX) {
        GG_LOGE("Can't encode list of len %zu.", list.len);
        return GG_ERR_RANGE;
    }
    uint32_t len = (uint32_t) list.len;

    // NOLINTNEXTLINE(bugprone-sizeof-expression)
    uint8_t *buf = GG_ARENA_ALLOCN(alloc, uint8_t, sizeof(len));
    if (buf == NULL) {
        GG_LOGE("Insufficient memory to encode packet.");
        return GG_ERR_NOMEM;
    }

    memcpy(buf, &len, sizeof(len));

    GgError ret = push_parse_state(
        state,
        (NestingLevel) {
            .type = HANDLING_OBJ,
            .obj_next = list.items,
            .remaining = len,
        }
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    return GG_ERR_OK;
}

static GgError read_list(
    GgArena *alloc, NestingState *state, GgBuffer *buf, GgObject *obj
) {
    GgBuffer temp_buf;
    uint32_t len;
    GgError ret = buf_take(sizeof(len), buf, &temp_buf);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    memcpy(&len, temp_buf.data, sizeof(len));

    GgList val = { .len = len };

    if (len > 0) {
        if (alloc == NULL) {
            GG_LOGE("Packet decode requires allocation and no alloc provided.");
            return GG_ERR_NOMEM;
        }

        val.items = GG_ARENA_ALLOCN(alloc, GgObject, len);
        if (val.items == NULL) {
            GG_LOGE("Insufficient memory to decode packet.");
            return GG_ERR_NOMEM;
        }

        ret = push_parse_state(
            state,
            (NestingLevel) {
                .type = HANDLING_OBJ,
                .obj_next = val.items,
                .remaining = len,
            }
        );
        if (ret != GG_ERR_OK) {
            return ret;
        }
    }

    *obj = gg_obj_list(val);
    return GG_ERR_OK;
}

static GgError write_map(GgArena *alloc, NestingState *state, GgMap map) {
    assert(alloc != NULL);

    if (map.len > UINT32_MAX) {
        GG_LOGE("Can't encode map of len %zu.", map.len);
        return GG_ERR_RANGE;
    }
    uint32_t len = (uint32_t) map.len;

    // NOLINTNEXTLINE(bugprone-sizeof-expression)
    uint8_t *buf = GG_ARENA_ALLOCN(alloc, uint8_t, sizeof(len));
    if (buf == NULL) {
        GG_LOGE("Insufficient memory to encode packet.");
        return GG_ERR_NOMEM;
    }

    memcpy(buf, &len, sizeof(len));

    GgError ret = push_parse_state(
        state,
        (NestingLevel) {
            .type = HANDLING_KV,
            .kv_next = map.pairs,
            .remaining = len,
        }
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }
    return GG_ERR_OK;
}

static GgError read_map(
    GgArena *alloc, NestingState *state, GgBuffer *buf, GgObject *obj
) {
    GgBuffer temp_buf;
    uint32_t len;
    GgError ret = buf_take(sizeof(len), buf, &temp_buf);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    memcpy(&len, temp_buf.data, sizeof(len));

    GgMap val = { .len = len };

    if (len > 0) {
        if (alloc == NULL) {
            GG_LOGE("Packet decode requires allocation and no alloc provided.");
            return GG_ERR_NOMEM;
        }

        val.pairs = GG_ARENA_ALLOCN(alloc, GgKV, len);
        if (val.pairs == NULL) {
            GG_LOGE("Insufficient memory to decode packet.");
            return GG_ERR_NOMEM;
        }

        ret = push_parse_state(
            state,
            (NestingLevel) {
                .type = HANDLING_KV,
                .kv_next = val.pairs,
                .remaining = len,
            }
        );
        if (ret != GG_ERR_OK) {
            return ret;
        }
    }

    *obj = gg_obj_map(val);
    return GG_ERR_OK;
}

static GgError write_obj(GgArena *alloc, NestingState *state, GgObject obj) {
    uint8_t *buf = GG_ARENA_ALLOCN(alloc, uint8_t, 1);
    if (buf == NULL) {
        GG_LOGE("Insufficient memory to encode packet.");
        return GG_ERR_NOMEM;
    }
    buf[0] = (uint8_t) gg_obj_type(obj);

    assert(alloc != NULL);
    switch (gg_obj_type(obj)) {
    case GG_TYPE_NULL:
        return GG_ERR_OK;
    case GG_TYPE_BOOLEAN:
        return write_bool(alloc, gg_obj_into_bool(obj));
    case GG_TYPE_I64:
        return write_i64(alloc, gg_obj_into_i64(obj));
    case GG_TYPE_F64:
        return write_f64(alloc, gg_obj_into_f64(obj));
    case GG_TYPE_BUF:
        return write_buf(alloc, gg_obj_into_buf(obj));
    case GG_TYPE_LIST:
        return write_list(alloc, state, gg_obj_into_list(obj));
    case GG_TYPE_MAP:
        return write_map(alloc, state, gg_obj_into_map(obj));
    }
    return GG_ERR_INVALID;
}

static GgError read_obj(
    GgArena *alloc, NestingState *state, GgBuffer *buf, GgObject *obj
) {
    assert((buf != NULL) && (obj != NULL));

    GgBuffer temp_buf;
    GgError ret = buf_take(1, buf, &temp_buf);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    uint8_t tag = temp_buf.data[0];

    switch (tag) {
    case GG_TYPE_NULL:
        *obj = GG_OBJ_NULL;
        return GG_ERR_OK;
    case GG_TYPE_BOOLEAN:
        return read_bool(buf, obj);
    case GG_TYPE_I64:
        return read_i64(buf, obj);
    case GG_TYPE_F64:
        return read_f64(buf, obj);
    case GG_TYPE_BUF:
        return read_buf(buf, obj);
    case GG_TYPE_LIST:
        return read_list(alloc, state, buf, obj);
    case GG_TYPE_MAP:
        return read_map(alloc, state, buf, obj);
    default:
        break;
    }
    return GG_ERR_INVALID;
}

GgError ggl_serialize(GgObject obj, GgBuffer *buf) {
    assert(buf != NULL);
    // TODO: Remove alloc abuse. Should use a writer.
    GgArena mem = gg_arena_init(*buf);

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
            GgError ret = write_obj(&mem, &state, *level->obj_next);
            if (ret != GG_ERR_OK) {
                return ret;
            }
            level->remaining -= 1;
            level->obj_next = &level->obj_next[1];
        } else if (level->type == HANDLING_KV) {
            GgError ret = write_buf(&mem, gg_kv_key(*level->kv_next));
            if (ret != GG_ERR_OK) {
                return ret;
            }
            ret = write_obj(&mem, &state, *gg_kv_val(level->kv_next));
            if (ret != GG_ERR_OK) {
                return ret;
            }
            level->remaining -= 1;
            level->kv_next = &level->kv_next[1];
        } else {
            assert(false);
        }
    } while (state.level > 0);

    buf->len = mem.index;
    return GG_ERR_OK;
}

GgError ggl_deserialize(GgArena *alloc, GgBuffer buf, GgObject *obj) {
    assert(obj != NULL);

    GgBuffer rest = buf;

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
            GgError ret = read_obj(alloc, &state, &rest, level->obj_next);
            if (ret != GG_ERR_OK) {
                return ret;
            }
            level->remaining -= 1;
            level->obj_next = &level->obj_next[1];
        } else if (level->type == HANDLING_KV) {
            GgBuffer key = { 0 };
            GgError ret = read_buf_raw(&rest, &key);
            if (ret != GG_ERR_OK) {
                return ret;
            }
            gg_kv_set_key(level->kv_next, key);

            ret = read_obj(alloc, &state, &rest, gg_kv_val(level->kv_next));
            if (ret != GG_ERR_OK) {
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
        GG_LOGE("Payload has %zu trailing bytes.", rest.len);
        return GG_ERR_PARSE;
    }

    return GG_ERR_OK;
}

static GgError obj_read(void *ctx, GgBuffer *buf) {
    assert(buf != NULL);

    GgObject *obj = ctx;

    if ((obj == NULL) || (buf == NULL)) {
        return GG_ERR_INVALID;
    }

    return ggl_serialize(*obj, buf);
}

GgReader ggl_serialize_reader(GgObject *obj) {
    assert(obj != NULL);
    return (GgReader) { .read = obj_read, .ctx = obj };
}
