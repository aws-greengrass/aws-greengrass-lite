// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/types.h>
#include <gg/vector.h>
#include <ggl/binpath.h>
#include <stddef.h>

GgError ggl_binpath_get_dir(GgBuffer argv0, GgByteVec *result) {
    if (argv0.data == NULL || result == NULL) {
        return GG_ERR_INVALID;
    }

    // Find last slash in argv0
    size_t last_slash = 0;
    for (size_t i = 0; i < argv0.len; i++) {
        if (argv0.data[i] == '/') {
            last_slash = i + 1;
        }
    }

    GgBuffer dir = gg_buffer_substr(argv0, 0, last_slash);
    return gg_byte_vec_append(result, dir);
}

GgError ggl_binpath_append_name(
    GgBuffer argv0, GgBuffer name, GgByteVec *result
) {
    if (name.data == NULL) {
        return GG_ERR_INVALID;
    }

    GgError err = ggl_binpath_get_dir(argv0, result);
    if (err != GG_ERR_OK) {
        return err;
    }

    return gg_byte_vec_append(result, name);
}
