// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/json_encode.h"
#include <assert.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static GglError json_write(GglObject obj, GglBuffer *buf);

static GglError buf_write(GglBuffer str, GglBuffer *buf) {
    if (buf->len < str.len) {
        GGL_LOGE("json", "Insufficient buffer space to encode json.");
        return GGL_ERR_NOMEM;
    }

    memcpy(buf->data, str.data, str.len);
    *buf = ggl_buffer_substr(*buf, str.len, SIZE_MAX);

    return GGL_ERR_OK;
}

static GglError json_write_null(GglBuffer *buf) {
    GglError ret = buf_write(GGL_STR("null"), buf);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    return GGL_ERR_OK;
}

static GglError json_write_bool(bool b, GglBuffer *buf) {
    GglError ret = buf_write(b ? GGL_STR("true") : GGL_STR("false"), buf);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    return GGL_ERR_OK;
}

static GglError json_write_i64(int64_t i64, GglBuffer *buf) {
    int ret = snprintf((char *) buf->data, buf->len, "%ld", i64);
    if (ret < 0) {
        GGL_LOGE("json", "Error encoding json.");
        return GGL_ERR_FAILURE;
    }
    if ((size_t) ret > buf->len) {
        GGL_LOGE("json", "Insufficient buffer space to encode json.");
        return GGL_ERR_NOMEM;
    }
    *buf = ggl_buffer_substr(*buf, (size_t) ret, SIZE_MAX);
    return GGL_ERR_OK;
}

static GglError json_write_f64(double f64, GglBuffer *buf) {
    int ret = snprintf((char *) buf->data, buf->len, "%g", f64);
    if (ret < 0) {
        GGL_LOGE("json", "Error encoding json.");
        return GGL_ERR_FAILURE;
    }
    if ((size_t) ret > buf->len) {
        GGL_LOGE("json", "Insufficient buffer space to encode json.");
        return GGL_ERR_NOMEM;
    }
    *buf = ggl_buffer_substr(*buf, (size_t) ret, SIZE_MAX);
    return GGL_ERR_OK;
}

static GglError json_write_buf(GglBuffer str, GglBuffer *buf) {
    GglError ret = buf_write(GGL_STR("\""), buf);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    for (size_t i = 0; i < str.len; i++) {
        uint8_t byte = str.data[i];
        if ((char) byte == '"') {
            ret = buf_write(GGL_STR("\\\""), buf);
            if (ret != GGL_ERR_OK) {
                return ret;
            }
        } else if ((char) byte == '\\') {
            ret = buf_write(GGL_STR("\\\\"), buf);
            if (ret != GGL_ERR_OK) {
                return ret;
            }
        } else if (byte <= 0x001F) {
            int ret_len
                = snprintf((char *) buf->data, buf->len, "\\u00%02X", byte);
            if (ret_len < 0) {
                GGL_LOGE("json", "Error encoding json.");
                return GGL_ERR_FAILURE;
            }
            if ((size_t) ret_len > buf->len) {
                GGL_LOGE("json", "Insufficient buffer space to encode json.");
                return GGL_ERR_NOMEM;
            }
            *buf = ggl_buffer_substr(*buf, (size_t) ret_len, SIZE_MAX);
        } else {
            ret = buf_write((GglBuffer) { .data = &byte, .len = 1 }, buf);
            if (ret != GGL_ERR_OK) {
                return ret;
            }
        }
    }

    ret = buf_write(GGL_STR("\""), buf);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    return GGL_ERR_OK;
}

// NOLINTNEXTLINE(misc-no-recursion)
static GglError json_write_list(GglList list, GglBuffer *buf) {
    GglError ret = buf_write(GGL_STR("["), buf);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    for (size_t i = 0; i < list.len; i++) {
        if (i != 0) {
            ret = buf_write(GGL_STR(","), buf);
            if (ret != GGL_ERR_OK) {
                return ret;
            }
        }

        ret = json_write(list.items[i], buf);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    ret = buf_write(GGL_STR("]"), buf);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    return GGL_ERR_OK;
}

// NOLINTNEXTLINE(misc-no-recursion)
static GglError json_write_map(GglMap map, GglBuffer *buf) {
    GglError ret = buf_write(GGL_STR("{"), buf);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    for (size_t i = 0; i < map.len; i++) {
        if (i != 0) {
            ret = buf_write(GGL_STR(","), buf);
            if (ret != GGL_ERR_OK) {
                return ret;
            }
        }
        ret = json_write(GGL_OBJ(map.pairs[i].key), buf);
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        ret = buf_write(GGL_STR(":"), buf);
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        ret = json_write(map.pairs[i].val, buf);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    ret = buf_write(GGL_STR("}"), buf);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    return GGL_ERR_OK;
}

// NOLINTNEXTLINE(misc-no-recursion)
static GglError json_write(GglObject obj, GglBuffer *buf) {
    switch (obj.type) {
    case GGL_TYPE_NULL:
        return json_write_null(buf);
    case GGL_TYPE_BOOLEAN:
        return json_write_bool(obj.boolean, buf);
    case GGL_TYPE_I64:
        return json_write_i64(obj.i64, buf);
    case GGL_TYPE_F64:
        return json_write_f64(obj.f64, buf);
    case GGL_TYPE_BUF:
        return json_write_buf(obj.buf, buf);
    case GGL_TYPE_LIST:
        return json_write_list(obj.list, buf);
    case GGL_TYPE_MAP:
        return json_write_map(obj.map, buf);
    }
    assert(false);
    return GGL_ERR_FAILURE;
}

GglError ggl_json_encode(GglObject obj, GglBuffer *buf) {
    GglBuffer buf_copy = *buf;
    GglError ret = json_write(obj, &buf_copy);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    buf->len = (size_t) (buf_copy.data - buf->data);
    return GGL_ERR_OK;
}
