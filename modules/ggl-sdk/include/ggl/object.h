// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_OBJECT_H
#define GGL_OBJECT_H

//! Generic dynamic object representation.

#include <ggl/attr.h>
#include <ggl/buffer.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/// A generic object.
typedef struct {
    // Used only with memcpy so no aliasing with contents
    uint8_t _private[(sizeof(void *) == 4) ? 9 : 11];
} GglObject;

/// Type tag for `GglObject`.
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
typedef struct __attribute__((may_alias)) {
    // KVs alias with pointers to their value objects
    uint8_t _private[sizeof(void *) + 2 + sizeof(GglObject)];
} GglKV;

/// A map of UTF-8 strings to `GglObject`s.
typedef struct {
    GglKV *pairs;
    size_t len;
} GglMap;

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
GglObjectType ggl_obj_type(GglObject obj) CONST;

static const GglObject GGL_OBJ_NULL UNUSED = { 0 };

/// Create bool object.
GglObject ggl_obj_bool(bool value) CONST;

/// Get the bool represented by an object.
/// The GglObject must be of type GGL_TYPE_BOOLEAN.
bool ggl_obj_into_bool(GglObject boolean) CONST;

/// Create signed integer object.
GglObject ggl_obj_i64(int64_t value) CONST;

/// Get the i64 represented by an object.
/// The GglObject must be of type GGL_TYPE_I64.
int64_t ggl_obj_into_i64(GglObject i64) CONST;

/// Create floating point object.
GglObject ggl_obj_f64(double value) CONST;

/// Get the f64 represented by an object.
/// The GglObject must be of type GGL_TYPE_F64.
double ggl_obj_into_f64(GglObject f64) CONST;

/// Create buffer object.
GglObject ggl_obj_buf(GglBuffer value) CONST;

/// Get the buffer represented by an object.
/// The GglObject must be of type GGL_TYPE_BUF.
GglBuffer ggl_obj_into_buf(GglObject buf) CONST;

/// Create map object.
GglObject ggl_obj_map(GglMap value) CONST;

/// Get the map represented by an object.
/// The GglObject must be of type GGL_TYPE_MAP.
GglMap ggl_obj_into_map(GglObject map) CONST;

/// Create list object.
GglObject ggl_obj_list(GglList value) CONST;

/// Get the list represented by an object.
/// The GglObject must be of type GGL_TYPE_LIST.
GglList ggl_obj_into_list(GglObject list) CONST;

#endif
