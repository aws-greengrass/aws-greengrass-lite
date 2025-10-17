// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <ggl/binpath.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/vector.h>
#include <stddef.h>

GglError ggl_binpath_get_dir(GglBuffer argv0, GglByteVec *result) {
    if (argv0.data == NULL || result == NULL) {
        return GGL_ERR_INVALID;
    }

    // Find last slash in argv0
    size_t last_slash = 0;
    for (size_t i = 0; i < argv0.len; i++) {
        if (argv0.data[i] == '/') {
            last_slash = i + 1;
        }
    }

    GglBuffer dir = ggl_buffer_substr(argv0, 0, last_slash);
    return ggl_byte_vec_append(result, dir);
}

GglError ggl_binpath_append_name(
    GglBuffer argv0, GglBuffer name, GglByteVec *result
) {
    if (name.data == NULL) {
        return GGL_ERR_INVALID;
    }

    GglError err = ggl_binpath_get_dir(argv0, result);
    if (err != GGL_ERR_OK) {
        return err;
    }

    return ggl_byte_vec_append(result, name);
}
