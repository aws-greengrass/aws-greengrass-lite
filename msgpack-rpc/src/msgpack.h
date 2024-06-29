/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_MSGPACK_H
#define GGL_MSGPACK_H

#include "ggl/alloc.h"
#include "ggl/error.h"
#include "ggl/object.h"

/** Maximum size of msgpack payload to read/write.
 * Can be configured with `-DGGL_MSGPACK_MAX_MSG_LEN=<N>`. */
#ifndef GGL_MSGPACK_MAX_MSG_LEN
#define GGL_MSGPACK_MAX_MSG_LEN 10000
#endif

/** Writes object to a buffer in msgpack encoding.
 * `buf` must be initialized to the buffer to write to. Its length will be
 * updated to the length of the encoded data. */
GglError ggl_msgpack_encode(GglObject obj, GglBuffer *buf);

/** Decodes msgpack data into an object.
 * All returned data is allocated with `alloc`, and does not reference `buf`. */
GglError ggl_msgpack_decode(GglAlloc *alloc, GglBuffer buf, GglObject *obj);

/** Partially decodes msgpack data, and updates `buf` to remaining data.
 * Returned buffers point into `buf`.
 * Returned Lists and Maps have NULL for elements, and should only be used for
 * length. Reading is stopped before any List/Map contained elements. */
GglError ggl_msgpack_decode_lazy_noalloc(GglBuffer *buf, GglObject *obj);

#endif
