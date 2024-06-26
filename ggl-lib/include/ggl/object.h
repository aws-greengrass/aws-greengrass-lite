/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_OBJECT_H
#define GGL_OBJECT_H

/*! Generic dynamic object representation. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Union tag for `Ggl_Object`. */
enum GglObjectType {
    GGL_TYPE_NULL = 0,
    GGL_TYPE_BOOLEAN,
    GGL_TYPE_U64,
    GGL_TYPE_I64,
    GGL_TYPE_F64,
    GGL_TYPE_BUF,
    GGL_TYPE_LIST,
    GGL_TYPE_MAP,
};

/** A fixed buffer of bytes. Possibly a string. */
typedef struct {
    uint8_t *data;
    size_t len;
} GglBuffer;

/** An array of `Ggl_Object`. */
typedef struct {
    struct GglObject *items;
    size_t len;
} GglList;

/** A map of UTF-8 strings to `Ggl_Object`s. */
typedef struct {
    struct GglKV *pairs;
    size_t len;
} GglMap;

/** A generic object. */
typedef struct GglObject {
    enum GglObjectType type;

    union {
        bool boolean;
        uint64_t u64;
        int64_t i64;
        double f64;
        GglBuffer buf;
        GglList list;
        GglMap map;
    };
} GglObject;

/** A key-value pair used for `Ggl_Map`.
 * `key` must be an UTF-8 encoded string. */
typedef struct GglKV {
    GglBuffer key;
    GglObject val;
} GglKV;

// statement expression needed as only way to tell if an expression is a string
// literal is to assign it to a char array declaration. Note that a initializer
// expression for array of char would also pass this, which is unfortunate.

/** Create buffer literal from a string literal. */
#define GGL_STR(strlit) \
    __extension__({ \
        char temp[] __attribute((unused)) = (strlit); \
        (GglBuffer) { \
            .data = (uint8_t *) (strlit), \
            .len = sizeof(strlit) - 1U, \
        }; \
    })

// generic function on pointer is to validate parameter is array and not ptr.
// On systems where char == uint8_t, this won't warn on string literal.

/** Create buffer literal from a byte array. */
#define GGL_BUF(...) \
    _Generic( \
        (&(__VA_ARGS__)), \
        uint8_t(*)[]: ((GglBuffer) { .data = (__VA_ARGS__), \
                                     .len = sizeof(__VA_ARGS__) }) \
    )

/** Create list literal from object literals. */
#define GGL_LIST(...) \
    (GglList) { \
        .items = (GglObject[]) { __VA_ARGS__ }, \
        .len = (sizeof((GglObject[]) { __VA_ARGS__ })) / (sizeof(GglObject)) \
    }

/** Create map literal from key-value literals. */
#define GGL_MAP(...) \
    (GglMap) { \
        .pairs = (GglKV[]) { __VA_ARGS__ }, \
        .len = (sizeof((GglKV[]) { __VA_ARGS__ })) / (sizeof(GglKV)) \
    }

/** Create null object literal. */
#define GGL_OBJ_NULL() \
    (GglObject) { \
        .type = GGL_TYPE_NULL \
    }

/** Create bool object literal. */
#define GGL_OBJ_BOOL(value) \
    (GglObject) { \
        .type = GGL_TYPE_BOOLEAN, .boolean = (value) \
    }

/** Create unsigned integer object literal. */
#define GGL_OBJ_U64(value) \
    (GglObject) { \
        .type = GGL_TYPE_U64, .u64 = (value) \
    }

/** Create signed integer object literal. */
#define GGL_OBJ_I64(value) \
    (GglObject) { \
        .type = GGL_TYPE_I64, .i64 = (value) \
    }

/** Create floating point object literal. */
#define GGL_OBJ_F64(value) \
    (GglObject) { \
        .type = GGL_TYPE_F64, .f64 = (value) \
    }

/** Create buffer object literal from a string literal. */
#define GGL_OBJ_STR(strlit) \
    (GglObject) { \
        .type = GGL_TYPE_BUF, .buf = GGL_STR(strlit), \
    }

/** Create buffer object literal from a byte array. */
#define GGL_OBJ_BUF(...) \
    (GglObject) { \
        .type = GGL_TYPE_BUF, .buf = GGL_BUF(__VA_ARGS__), \
    }

/** Create map object literal from `Ggl_KV` literals. */
#define GGL_OBJ_MAP(...) \
    (GglObject) { \
        .type = GGL_TYPE_MAP, .map = GGL_MAP(__VA_ARGS__), \
    }

/** Create list object literal from object literals. */
#define GGL_OBJ_LIST(...) \
    (GglObject) { \
        .type = GGL_TYPE_LIST, .list = GGL_LIST(__VA_ARGS__), \
    }

// NOLINTBEGIN(bugprone-macro-parentheses)
#define GGL_FORCE(type, value) \
    _Generic((value), type: (value), default: (type) { 0 })
// NOLINTEND(bugprone-macro-parentheses)

/** Create object literal from buffer, list, or map. */
#define GGL_OBJ(...) \
    _Generic( \
        (__VA_ARGS__), \
        GglBuffer: (GglObject) { .type = GGL_TYPE_BUF, \
                                 .buf = GGL_FORCE(GglBuffer, (__VA_ARGS__)) }, \
        GglList: (GglObject) { .type = GGL_TYPE_LIST, \
                               .list = GGL_FORCE(GglList, (__VA_ARGS__)) }, \
        GglMap: (GglObject) { .type = GGL_TYPE_MAP, \
                              .map = GGL_FORCE(GglMap, (__VA_ARGS__)) } \
    )

#endif
