// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/recipe.h"
#include <sys/types.h>
#include <fcntl.h>
#include <ggl/alloc.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/json_decode.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <ggl/yaml_decode.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

static GglError try_open_extension(
    int recipe_dir, GglBuffer ext, GglByteVec name, GglBuffer *content
) {
    GglByteVec full = name;
    GglError ret = ggl_byte_vec_push(&full, '.');
    ggl_byte_vec_chain_append(&ret, &full, ext);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return ggl_file_read_path_at(recipe_dir, full.buf, content);
}

GglError ggl_recipe_get_from_file(
    int root_path,
    GglBuffer component_name,
    GglBuffer component_version,
    GglAlloc *alloc,
    GglObject *recipe
) {
    static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&mtx);
    GGL_DEFER(pthread_mutex_unlock, mtx);

    int recipe_dir;
    GglError ret = ggl_dir_openat(
        root_path, GGL_STR("packages/recipes"), O_PATH, false, &recipe_dir
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to open recipe dir.");
        return ret;
    }

    static uint8_t file_name_mem[PATH_MAX];
    GglByteVec base_name = GGL_BYTE_VEC(file_name_mem);

    ggl_byte_vec_chain_append(&ret, &base_name, component_name);
    ggl_byte_vec_chain_push(&ret, &base_name, '-');
    ggl_byte_vec_chain_append(&ret, &base_name, component_version);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Recipe path too long.");
        return ret;
    }

    static uint8_t file_mem[8196];
    GglBuffer content = GGL_BUF(file_mem);
    ret = try_open_extension(recipe_dir, GGL_STR("json"), base_name, &content);
    if (ret == GGL_ERR_OK) {
        ret = ggl_json_decode_destructive(content, alloc, recipe);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    } else {
        ret = try_open_extension(
            recipe_dir, GGL_STR("yaml"), base_name, &content
        );

        if (ret != GGL_ERR_OK) {
            ret = try_open_extension(
                recipe_dir, GGL_STR("yml"), base_name, &content
            );
            if (ret != GGL_ERR_OK) {
                return ret;
            }
        }

        ret = ggl_yaml_decode_destructive(content, alloc, recipe);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    return ggl_obj_buffer_copy(recipe, alloc);
}
