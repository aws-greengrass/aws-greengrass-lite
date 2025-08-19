// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/json_pointer.h"
#include <assert.h>
#include <ggl/buffer.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <stddef.h>
#include <stdint.h>

GglError ggl_gg_config_jsonp_parse(GglBuffer json_ptr, GglBufVec *key_path) {
    assert(key_path->capacity == GGL_MAX_OBJECT_DEPTH);

    // TODO: Do full parsing of JSON pointer

    if ((json_ptr.len < 1) || (json_ptr.data[0] != '/')) {
        GGL_LOGE("Invalid json pointer.");
        return GGL_ERR_FAILURE;
    }

    size_t begin = 1;
    for (size_t i = 1; i < json_ptr.len; i++) {
        if (json_ptr.data[i] == '/') {
            GglError ret = ggl_buf_vec_push(
                key_path, ggl_buffer_substr(json_ptr, begin, i)
            );
            if (ret != GGL_ERR_OK) {
                GGL_LOGE("Too many configuration levels.");
                return ret;
            }
            begin = i + 1;
        }
    }
    GglError ret = ggl_buf_vec_push(
        key_path, ggl_buffer_substr(json_ptr, begin, SIZE_MAX)
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Too many configuration levels.");
        return ret;
    }

    return GGL_ERR_OK;
}
