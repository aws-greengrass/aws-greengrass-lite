// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include "file_operation.h"
#include <fcntl.h>
#include <ggl/error.h>
#include <ggl/json_decode.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/yaml_decode.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdint.h>

// NOLINTNEXTLINE(misc-no-recursion)
static void ggl_key_to_lower(GglObject object_object_to_lower) {
    if (object_object_to_lower.type == GGL_TYPE_MAP) {
        // Iterate over the pairs of Map
        for (size_t count = 0; count < object_object_to_lower.map.len;
             count++) {
            // Iterate through the string
            for (size_t key_count = 0;
                 key_count < object_object_to_lower.map.pairs[count].key.len;
                 key_count++) {
                if (object_object_to_lower.map.pairs[count].key.data[key_count]
                        >= 'A'
                    && object_object_to_lower.map.pairs[count]
                            .key.data[key_count]
                        <= 'Z') {
                    object_object_to_lower.map.pairs[count].key.data[key_count]
                        = object_object_to_lower.map.pairs[count]
                              .key.data[key_count]
                        + ('a' - 'A');
                }
            }
            ggl_key_to_lower(object_object_to_lower.map.pairs[count].val);
        }
    } else if (object_object_to_lower.type == GGL_TYPE_LIST) {
        // Iterate over the List
        for (size_t count = 0; count < object_object_to_lower.list.len;
             count++) {
            ggl_key_to_lower(object_object_to_lower.list.items[count]);
        }
    }
}

static GglError deserialize_json(
    GglBuffer recipe_buffer, GglAlloc *alloc, GglObject *recipe_obj
) {
    ggl_json_decode_destructive(recipe_buffer, alloc, recipe_obj);
    if (recipe_obj->type != GGL_TYPE_MAP) {
        return GGL_ERR_FAILURE;
    }

    ggl_key_to_lower(*recipe_obj);

    return GGL_ERR_OK;
}

static GglError deserialize_yaml(
    GglBuffer recipe_buffer, GglAlloc *alloc, GglObject *recipe_obj
) {
    ggl_yaml_decode_destructive(recipe_buffer, alloc, recipe_obj);
    if (recipe_obj->type != GGL_TYPE_MAP) {
        return GGL_ERR_FAILURE;
    }

    ggl_key_to_lower(*recipe_obj);

    return GGL_ERR_OK;
}

GglError deserialize_file_content(
    char *file_path,
    GglBuffer recipe_str_buf,
    GglAlloc *alloc,
    GglObject *recipe_obj
) {
    GglError ret;
    // Find the last occurrence of a dot in the filepath
    const char *dot = strrchr(file_path, '.');
    if (!dot || dot == file_path) {
        return GGL_ERR_INVALID; // No extension found or dot is at the start
    }

    if (strcmp(dot, ".json") == 0) {
        ret = deserialize_json(recipe_str_buf, alloc, recipe_obj);
        if (ret != GGL_ERR_OK) {
            return ret;
        }

    } else if (strcmp(dot, ".yaml") == 0 || strcmp(dot, ".yml") == 0) {
        ret = deserialize_yaml(recipe_str_buf, alloc, recipe_obj);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    } else {
        return GGL_ERR_INVALID;
    }
    return GGL_ERR_OK;
}

GglError open_file(char *file_path, GglBuffer *recipe_obj) {
    int fd = open(file_path, O_RDONLY);
    if (fd == -1) {
        GGL_LOGE("main", "Failed to open recipe file.");
        return GGL_ERR_FAILURE;
    }

    struct stat st;
    if (fstat(fd, &st) == -1) {
        GGL_LOGE("main", "Failed to get recipe file info.");
        return GGL_ERR_FAILURE;
    }

    size_t file_size = (size_t) st.st_size;
    uint8_t *file_str
        = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    if (file_str == MAP_FAILED) {
        GGL_LOGE("main", "Failed to load recipe file.");
        return GGL_ERR_FAILURE;
    }

    *recipe_obj = (GglBuffer) { .data = file_str, .len = file_size };

    return GGL_ERR_OK;
}
