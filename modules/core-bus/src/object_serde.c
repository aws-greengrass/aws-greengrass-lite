// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "object_serde.h"
#include <assert.h>
#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/io.h>
#include <ggl/log.h>
#include <ggl/map.h>
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

static GglError write_bool(GglArena *alloc, bool boolean) {
    assert(alloc != NULL);

    uint8_t *buf = GGL_ARENA_ALLOCN(alloc, uint8_t, 1);
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
    *obj = ggl_obj_bool(temp_buf.data[0]);
    return GGL_ERR_OK;
}

static GglError write_i64(GglArena *alloc, int64_t i64) {
    assert(alloc != NULL);

    uint8_t *buf = GGL_ARENA_ALLOCN(alloc, uint8_t, sizeof(int64_t));
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
    *obj = ggl_obj_i64(val);
    return GGL_ERR_OK;
}

static GglError write_f64(GglArena *alloc, double f64) {
    assert(alloc != NULL);

    uint8_t *buf = GGL_ARENA_ALLOCN(alloc, uint8_t, sizeof(double));
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
    *obj = ggl_obj_f64(val);
    return GGL_ERR_OK;
}

static GglError write_buf(GglArena *alloc, GglBuffer buffer) {
    assert(alloc != NULL);

    if (buffer.len > UINT32_MAX) {
        GGL_LOGE("Can't encode buffer of len %zu.", buffer.len);
        return GGL_ERR_RANGE;
    }
    uint32_t len = (uint32_t) buffer.len;

    uint8_t *buf = GGL_ARENA_ALLOCN(alloc, uint8_t, sizeof(len) + buffer.len);
    if (buf == NULL) {
        GGL_LOGE("Insufficient memory to encode packet.");
        return GGL_ERR_NOMEM;
    }

    memcpy(buf, &len, sizeof(len));
    memcpy(&buf[sizeof(len)], buffer.data, len);
    return GGL_ERR_OK;
}

static GglError read_buf_raw(GglBuffer *buf, GglBuffer *out) {
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

    *out = temp_buf;
    return GGL_ERR_OK;
}

static GglError read_buf(GglBuffer *buf, GglObject *obj) {
    GglBuffer val;
    GglError ret = read_buf_raw(buf, &val);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    *obj = ggl_obj_buf(val);
    return GGL_ERR_OK;
}

static GglError write_list(GglArena *alloc, NestingState *state, GglList list) {
    assert(alloc != NULL);

    if (list.len > UINT32_MAX) {
        GGL_LOGE("Can't encode list of len %zu.", list.len);
        return GGL_ERR_RANGE;
    }
    uint32_t len = (uint32_t) list.len;

    uint8_t *buf = GGL_ARENA_ALLOCN(alloc, uint8_t, sizeof(len));
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
    GglArena *alloc, NestingState *state, GglBuffer *buf, GglObject *obj
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

        val.items = GGL_ARENA_ALLOCN(alloc, GglObject, len);
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

    *obj = ggl_obj_list(val);
    return GGL_ERR_OK;
}

static GglError write_map(GglArena *alloc, NestingState *state, GglMap map) {
    assert(alloc != NULL);

    if (map.len > UINT32_MAX) {
        GGL_LOGE("Can't encode map of len %zu.", map.len);
        return GGL_ERR_RANGE;
    }
    uint32_t len = (uint32_t) map.len;

    uint8_t *buf = GGL_ARENA_ALLOCN(alloc, uint8_t, sizeof(len));
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
    GglArena *alloc, NestingState *state, GglBuffer *buf, GglObject *obj
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

        val.pairs = GGL_ARENA_ALLOCN(alloc, GglKV, len);
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

    *obj = ggl_obj_map(val);
    return GGL_ERR_OK;
}

static GglError write_obj(GglArena *alloc, NestingState *state, GglObject obj) {
    uint8_t *buf = GGL_ARENA_ALLOCN(alloc, uint8_t, 1);
    if (buf == NULL) {
        GGL_LOGE("Insufficient memory to encode packet.");
        return GGL_ERR_NOMEM;
    }
    buf[0] = (uint8_t) ggl_obj_type(obj);

    assert(alloc != NULL);
    switch (ggl_obj_type(obj)) {
    case GGL_TYPE_NULL:
        return GGL_ERR_OK;
    case GGL_TYPE_BOOLEAN:
        return write_bool(alloc, ggl_obj_into_bool(obj));
    case GGL_TYPE_I64:
        return write_i64(alloc, ggl_obj_into_i64(obj));
    case GGL_TYPE_F64:
        return write_f64(alloc, ggl_obj_into_f64(obj));
    case GGL_TYPE_BUF:
        return write_buf(alloc, ggl_obj_into_buf(obj));
    case GGL_TYPE_LIST:
        return write_list(alloc, state, ggl_obj_into_list(obj));
    case GGL_TYPE_MAP:
        return write_map(alloc, state, ggl_obj_into_map(obj));
    }
    return GGL_ERR_INVALID;
}

static GglError read_obj(
    GglArena *alloc, NestingState *state, GglBuffer *buf, GglObject *obj
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
        *obj = GGL_OBJ_NULL;
        return GGL_ERR_OK;
    case GGL_TYPE_BOOLEAN:
        return read_bool(buf, obj);
    case GGL_TYPE_I64:
        return read_i64(buf, obj);
    case GGL_TYPE_F64:
        return read_f64(buf, obj);
    case GGL_TYPE_BUF:
        return read_buf(buf, obj);
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
    assert(buf != NULL);
    // TODO: Remove alloc abuse. Should use a writer.
    GglArena mem = ggl_arena_init(*buf);

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
            GglError ret = write_obj(&mem, &state, *level->obj_next);
            if (ret != GGL_ERR_OK) {
                return ret;
            }
            level->remaining -= 1;
            level->obj_next = &level->obj_next[1];
        } else if (level->type == HANDLING_KV) {
            GglError ret = write_buf(&mem, ggl_kv_key(*level->kv_next));
            if (ret != GGL_ERR_OK) {
                return ret;
            }
            ret = write_obj(&mem, &state, *ggl_kv_val(level->kv_next));
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

GglError ggl_deserialize(GglArena *alloc, GglBuffer buf, GglObject *obj) {
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
            GglError ret = read_obj(alloc, &state, &rest, level->obj_next);
            if (ret != GGL_ERR_OK) {
                return ret;
            }
            level->remaining -= 1;
            level->obj_next = &level->obj_next[1];
        } else if (level->type == HANDLING_KV) {
            GglBuffer key = { 0 };
            GglError ret = read_buf_raw(&rest, &key);
            if (ret != GGL_ERR_OK) {
                return ret;
            }
            ggl_kv_set_key(level->kv_next, key);

            ret = read_obj(alloc, &state, &rest, ggl_kv_val(level->kv_next));
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

static GglError obj_read(void *ctx, GglBuffer *buf) {
    assert(buf != NULL);

    GglObject *obj = ctx;

    if ((obj == NULL) || (buf == NULL)) {
        return GGL_ERR_INVALID;
    }

    return ggl_serialize(*obj, buf);
}

GglReader ggl_serialize_reader(GglObject *obj) {
    assert(obj != NULL);
    return (GglReader) { .read = obj_read, .ctx = obj };
}
