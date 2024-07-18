/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ggl/buffer.h"
#include "ggl/object.h"
#include <string.h>
#include <stdbool.h>

bool ggl_buffer_eq(GglBuffer buf1, GglBuffer buf2) {
    if (buf1.len == buf2.len) {
        return memcmp(buf1.data, buf2.data, buf1.len) == 0;
    }
    return false;
}

GglBuffer ggl_buffer_substr(GglBuffer buf, size_t start, size_t end) {
    size_t start_trunc = start < buf.len ? start : buf.len;
    size_t end_trunc = end < buf.len ? end : buf.len;
    return (GglBuffer) {
        .data = &buf.data[start_trunc],
        .len = end_trunc >= start_trunc ? end_trunc - start_trunc : 0U,
    };
}
