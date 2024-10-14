// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_OBJECT_H
#define GGL_OBJECT_H

//! Generic dynamic object representation.

#include "alloc.h"
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __COVERITY__
#define GGL_DISABLE_MACRO_TYPE_CHECKING
#endif

/// Union tag for `GglObject`.
typedef enum {
    GGL_TYPE_NULL = 0,
    GGL_TYPE_BOOLEAN,
    GGL_TYPE_I64,
    GGL_TYPE_F64,
    GGL_TYPE_BUF,
    GGL_TYPE_LIST,
    GGL_TYPE_MAP,
} GglObjectType;

/// An array of `GglObject`.
typedef struct {
    struct GglObject *items;
    size_t len;
} GglList;

/// A map of UTF-8 strings to `GglObject`s.
typedef struct {
    struct GglKV *pairs;
    size_t len;
} GglMap;

/// A generic object.
typedef struct GglObject {
    GglObjectType type;

    union {
        bool boolean;
        int64_t i64;
        double f64;
        GglBuffer buf;
        GglList list;
        GglMap map;
    };
} GglObject;

/// A key-value pair used for `GglMap`.
/// `key` must be an UTF-8 encoded string.
typedef struct GglKV {
    GglBuffer key;
    GglObject val;
} GglKV;

// The only way to guarantee a string literal is with assignment; this could be
// done with a statement expression but those are not allowed at file context.

#define GGL_STR_UNCHECKED(strlit) \
    ((GglBuffer) { .data = (uint8_t *) (strlit), .len = sizeof(strlit) - 1U })

#ifndef GGL_DISABLE_MACRO_TYPE_CHECKING
/// Create buffer literal from a string literal.
#define GGL_STR(strlit) \
    _Generic( \
        (&(strlit)), \
        char(*)[]: GGL_STR_UNCHECKED(strlit), \
        const char(*)[]: GGL_STR_UNCHECKED(strlit) \
    )
#else
#define GGL_STR GGL_STR_UNCHECKED
#endif

// generic function on pointer is to validate parameter is array and not ptr.
// On systems where char == uint8_t, this won't warn on string literal.

#define GGL_BUF_UNCHECKED(...) \
    ((GglBuffer) { .data = (__VA_ARGS__), .len = sizeof(__VA_ARGS__) })

#ifndef GGL_DISABLE_MACRO_TYPE_CHECKING
/// Create buffer literal from a byte array.
#define GGL_BUF(...) \
    _Generic((&(__VA_ARGS__)), uint8_t(*)[]: GGL_BUF_UNCHECKED(__VA_ARGS__))
#else
#define GGL_BUF GGL_BUF_UNCHECKED
#endif

/// Create list literal from object literals.
#define GGL_LIST(...) \
    (GglList) { \
        .items = (GglObject[]) { __VA_ARGS__ }, \
        .len = (sizeof((GglObject[]) { __VA_ARGS__ })) / (sizeof(GglObject)) \
    }

/// Create map literal from key-value literals.
#define GGL_MAP(...) \
    (GglMap) { \
        .pairs = (GglKV[]) { __VA_ARGS__ }, \
        .len = (sizeof((GglKV[]) { __VA_ARGS__ })) / (sizeof(GglKV)) \
    }

/// Create null object literal.
#define GGL_OBJ_NULL() \
    (GglObject) { \
        .type = GGL_TYPE_NULL \
    }

/// Create bool object literal.
#define GGL_OBJ_BOOL(value) \
    (GglObject) { \
        .type = GGL_TYPE_BOOLEAN, .boolean = (value) \
    }

/// Create signed integer object literal.
#define GGL_OBJ_I64(value) \
    (GglObject) { \
        .type = GGL_TYPE_I64, .i64 = (value) \
    }

/// Create floating point object literal.
#define GGL_OBJ_F64(value) \
    (GglObject) { \
        .type = GGL_TYPE_F64, .f64 = (value) \
    }

/// Create buffer object literal from a string literal.
#define GGL_OBJ_STR(strlit) \
    (GglObject) { \
        .type = GGL_TYPE_BUF, .buf = GGL_STR(strlit), \
    }

/// Create buffer object literal from a byte array.
#define GGL_OBJ_BUF(...) \
    (GglObject) { \
        .type = GGL_TYPE_BUF, .buf = GGL_BUF(__VA_ARGS__), \
    }

/// Create map object literal from `Ggl_KV` literals.
#define GGL_OBJ_MAP(...) \
    (GglObject) { \
        .type = GGL_TYPE_MAP, .map = GGL_MAP(__VA_ARGS__), \
    }

/// Create list object literal from object literals.
#define GGL_OBJ_LIST(...) \
    (GglObject) { \
        .type = GGL_TYPE_LIST, .list = GGL_LIST(__VA_ARGS__), \
    }

#ifndef __COVERITY__
// NOLINTBEGIN(bugprone-macro-parentheses)
#define GGL_FORCE(type, value) \
    _Generic((value), type: (value), default: (type) { 0 })
// NOLINTEND(bugprone-macro-parentheses)

/// Create object literal from any contained type.
#define GGL_OBJ(...) \
    _Generic( \
        (__VA_ARGS__), \
        GglBuffer: (GglObject) { .type = GGL_TYPE_BUF, \
                                 .buf = GGL_FORCE(GglBuffer, (__VA_ARGS__)) }, \
        GglList: (GglObject) { .type = GGL_TYPE_LIST, \
                               .list = GGL_FORCE(GglList, (__VA_ARGS__)) }, \
        GglMap: (GglObject) { .type = GGL_TYPE_MAP, \
                              .map = GGL_FORCE(GglMap, (__VA_ARGS__)) }, \
        bool: (GglObject) { .type = GGL_TYPE_BOOLEAN, \
                            .boolean = GGL_FORCE(bool, (__VA_ARGS__)) }, \
        int64_t: (GglObject) { .type = GGL_TYPE_I64, \
                               .i64 = GGL_FORCE(int64_t, (__VA_ARGS__)) }, \
        double: (GglObject) { .type = GGL_TYPE_F64, \
                              .f64 = GGL_FORCE(double, (__VA_ARGS__)) } \
    )

#else
#define GGL_OBJ(...) \
    (GglObject) { \
        0 \
    }
#endif

/// Modifies an object's references to point to copies in alloc
GglError ggl_obj_deep_copy(GglObject *obj, GglAlloc *alloc);

/// Modifies an object's buffer references to point to copies in alloc
GglError ggl_obj_buffer_copy(GglObject *obj, GglAlloc *alloc);

#endif
