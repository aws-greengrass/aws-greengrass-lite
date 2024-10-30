// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include "file_operation.h"
#include <sys/types.h>
#include <fcntl.h>
#include <ggl/buffer.h>
#include <ggl/cleanup.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/json_decode.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/socket.h>
#include <ggl/yaml_decode.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stdint.h>

static GglError deserialize_json(
    GglBuffer recipe_buffer, GglAlloc *alloc, GglObject *recipe_obj
) {
    ggl_json_decode_destructive(recipe_buffer, alloc, recipe_obj);
    if (recipe_obj->type != GGL_TYPE_MAP) {
        return GGL_ERR_FAILURE;
    }

    return GGL_ERR_OK;
}

static GglError deserialize_yaml(
    GglBuffer recipe_buffer, GglAlloc *alloc, GglObject *recipe_obj
) {
    ggl_yaml_decode_destructive(recipe_buffer, alloc, recipe_obj);
    if (recipe_obj->type != GGL_TYPE_MAP) {
        return GGL_ERR_FAILURE;
    }

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
    int fd = open(file_path, O_RDONLY | O_CLOEXEC);
    if (fd == -1) {
        GGL_LOGE("Failed to open recipe file.");
        return GGL_ERR_FAILURE;
    }
    GGL_CLEANUP(cleanup_close, fd);

    struct stat st;
    if (fstat(fd, &st) == -1) {
        GGL_LOGE("Failed to get recipe file info.");
        return GGL_ERR_FAILURE;
    }

    size_t file_size = (size_t) st.st_size;
    uint8_t *file_str
        = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    if (file_str == MAP_FAILED) {
        GGL_LOGE("Failed to load recipe file.");
        return GGL_ERR_FAILURE;
    }

    *recipe_obj = (GglBuffer) { .data = file_str, .len = file_size };

    return GGL_ERR_OK;
}

GglError write_to_file(
    char *directory_path, GglBuffer filename, GglBuffer write_data, int mode
) {
    int root_dir_fd;
    GglError ret = ggl_dir_open(
        ggl_buffer_from_null_term(directory_path), O_PATH, true, &root_dir_fd
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to open dir");
        return GGL_ERR_FAILURE;
    }

    int script_as_file;

    ret = ggl_file_openat(
        root_dir_fd,
        filename,
        O_CREAT | O_WRONLY | O_TRUNC,
        (mode_t) mode,
        &script_as_file
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to open file at the dir");
        return GGL_ERR_FAILURE;
    }
    ret = ggl_socket_write_exact(script_as_file, write_data);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Write to file failed");
        return GGL_ERR_FAILURE;
    }
    ret = ggl_socket_write_exact(script_as_file, GGL_STR("\n"));
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Write to file failed");
        return GGL_ERR_FAILURE;
    }
    return GGL_ERR_OK;
}
