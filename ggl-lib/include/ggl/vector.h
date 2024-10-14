// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_VECTOR_H
#define GGL_VECTOR_H

//! Generic Object Vector interface

#include "error.h"
#include "object.h"
#include <ggl/buffer.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    GglList list;
    size_t capacity;
} GglObjVec;

#define GGL_OBJ_VEC_UNCHECKED(...) \
    ((GglObjVec) { .list = { .items = (__VA_ARGS__), .len = 0 }, \
                   .capacity = sizeof(__VA_ARGS__) / sizeof(GglObject) })

#ifndef GGL_DISABLE_MACRO_TYPE_CHECKING
#define GGL_OBJ_VEC(...) \
    _Generic( \
        (&(__VA_ARGS__)), GglObject(*)[]: GGL_OBJ_VEC_UNCHECKED(__VA_ARGS__) \
    )
#else
#define GGL_OBJ_VEC GGL_OBJ_VEC_UNCHECKED
#endif

GglError ggl_obj_vec_push(GglObjVec *vector, GglObject object);
void ggl_obj_vec_chain_push(GglError *err, GglObjVec *vector, GglObject object);
GglError ggl_obj_vec_pop(GglObjVec *vector, GglObject *out);
GglError ggl_obj_vec_append(GglObjVec *vector, GglList list);
void ggl_obj_vec_chain_append(GglError *err, GglObjVec *vector, GglList list);

typedef struct {
    GglMap map;
    size_t capacity;
} GglKVVec;

#define GGL_KV_VEC_UNCHECKED(...) \
    ((GglKVVec) { .map = { .pairs = (__VA_ARGS__), .len = 0 }, \
                  .capacity = sizeof(__VA_ARGS__) / sizeof(GglKV) })

#ifndef GGL_DISABLE_MACRO_TYPE_CHECKING
#define GGL_KV_VEC(...) \
    _Generic((&(__VA_ARGS__)), GglKV(*)[]: GGL_KV_VEC_UNCHECKED(__VA_ARGS__))
#else
#define GGL_KV_VEC GGL_KV_VEC_UNCHECKED
#endif

GglError ggl_kv_vec_push(GglKVVec *vector, GglKV kv);

typedef struct {
    GglBuffer buf;
    size_t capacity;
} GglByteVec;

#define GGL_BYTE_VEC_UNCHECKED(...) \
    ((GglByteVec) { .buf = { .data = (uint8_t *) (__VA_ARGS__), .len = 0 }, \
                    .capacity = sizeof(__VA_ARGS__) })

#ifndef GGL_DISABLE_MACRO_TYPE_CHECKING
#define GGL_BYTE_VEC(...) \
    _Generic( \
        (&(__VA_ARGS__)), \
        uint8_t(*)[]: GGL_BYTE_VEC_UNCHECKED(__VA_ARGS__), \
        char(*)[]: GGL_BYTE_VEC_UNCHECKED(__VA_ARGS__) \
    )
#else
#define GGL_BYTE_VEC GGL_BYTE_VEC_UNCHECKED
#endif

GglByteVec ggl_byte_vec_init(GglBuffer buf);
GglError ggl_byte_vec_push(GglByteVec *vector, uint8_t byte);
void ggl_byte_vec_chain_push(GglError *err, GglByteVec *vector, uint8_t byte);
GglError ggl_byte_vec_append(GglByteVec *vector, GglBuffer buf);
void ggl_byte_vec_chain_append(
    GglError *err, GglByteVec *vector, GglBuffer buf
);
GglBuffer ggl_byte_vec_remaining_capacity(GglByteVec vector);

typedef struct {
    GglBufList buf_list;
    size_t capacity;
} GglBufVec;

#define GGL_BUF_VEC_UNCHECKED(...) \
    ((GglBufVec) { .buf_list = { .bufs = (__VA_ARGS__), .len = 0 }, \
                   .capacity = sizeof(__VA_ARGS__) / sizeof(GglBuffer) })

#ifndef GGL_DISABLE_MACRO_TYPE_CHECKING
#define GGL_BUF_VEC(...) \
    _Generic( \
        (&(__VA_ARGS__)), GglBuffer(*)[]: GGL_BUF_VEC_UNCHECKED(__VA_ARGS__) \
    )
#else
#define GGL_BUF_VEC GGL_BUF_VEC_UNCHECKED
#endif

GglError ggl_buf_vec_push(GglBufVec *vector, GglBuffer buf);
void ggl_buf_vec_chain_push(GglError *err, GglBufVec *vector, GglBuffer buf);
GglError ggl_buf_vec_append_list(GglBufVec *vector, GglList list);
void ggl_buf_vec_chain_append_list(
    GglError *err, GglBufVec *vector, GglList list
);

#endif
