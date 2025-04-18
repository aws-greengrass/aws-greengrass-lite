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

/// A generic object.
typedef struct {
    union {
        struct {
            void *_a;
            size_t _b;
        };

        uint64_t _c;
    };

    uint8_t _d;
} __attribute__((may_alias)) GglObject;

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
    GglObject *items;
    size_t len;
} GglList;

/// A key-value pair used for `GglMap`.
/// `key` must be an UTF-8 encoded string.
typedef struct {
    GglBuffer key;
    GglObject val;
} GglKV;

/// A map of UTF-8 strings to `GglObject`s.
typedef struct {
    GglKV *pairs;
    size_t len;
} GglMap;

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

/// Get type of an GglObject
GglObjectType ggl_obj_type(GglObject obj);

static const GglObject GGL_OBJ_NULL = { 0 };

/// Create bool object.
GglObject ggl_obj_bool(bool value);

/// Get the bool represented by an object.
/// The GglObject must be of type GGL_TYPE_BOOLEAN.
bool ggl_obj_into_bool(GglObject boolean);

/// Create signed integer object.
GglObject ggl_obj_i64(int64_t value);

/// Get the i64 represented by an object.
/// The GglObject must be of type GGL_TYPE_I64.
int64_t ggl_obj_into_i64(GglObject i64);

/// Create floating point object.
GglObject ggl_obj_f64(double value);

/// Get the f64 represented by an object.
/// The GglObject must be of type GGL_TYPE_F64.
double ggl_obj_into_f64(GglObject f64);

/// Create buffer object.
GglObject ggl_obj_buf(GglBuffer value);

/// Get the buffer represented by an object.
/// The GglObject must be of type GGL_TYPE_BUF.
GglBuffer ggl_obj_into_buf(GglObject buf);

/// Create map object.
GglObject ggl_obj_map(GglMap value);

/// Get the map represented by an object.
/// The GglObject must be of type GGL_TYPE_MAP.
GglMap ggl_obj_into_map(GglObject map);

/// Create list object.
GglObject ggl_obj_list(GglList value);

/// Get the list represented by an object.
/// The GglObject must be of type GGL_TYPE_LIST.
GglList ggl_obj_into_list(GglObject list);

/// Modifies an object's references to point to copies in alloc
GglError ggl_obj_deep_copy(GglObject *obj, GglAlloc *alloc);

/// Modifies an object's buffer references to point to copies in alloc
GglError ggl_obj_buffer_copy(GglObject *obj, GglAlloc *alloc);

#endif
