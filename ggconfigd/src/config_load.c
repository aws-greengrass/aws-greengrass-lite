// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "dirent.h"
#include "ggconfigd.h"
#include <fcntl.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/cleanup.h>
#include <ggl/constants.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <ggl/yaml_decode.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

static GglError ggconfig_load_file_fd(int fd) {
    static uint8_t file_mem[8192];
    GglBuffer config_file = GGL_BUF(file_mem);

    GglError ret = ggl_file_read(fd, &config_file);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to read config file.");
        return GGL_ERR_FAILURE;
    }

    static uint8_t decode_mem[500 * sizeof(GglObject)];
    GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(decode_mem));

    GglObject config_obj;
    ret = ggl_yaml_decode_destructive(config_file, &balloc.alloc, &config_obj);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to parse config file.");
        return GGL_ERR_FAILURE;
    }

    GglObjVec key_path = GGL_OBJ_VEC((GglObject[GGL_MAX_OBJECT_DEPTH]) { 0 });

    if (config_obj.type == GGL_TYPE_MAP) {
        ret = process_map(&key_path, &config_obj.map, 2);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    } else {
        ret = process_nonmap(&key_path, config_obj, 2);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    return GGL_ERR_OK;
}

GglError ggconfig_load_file(GglBuffer path) {
    int fd;
    GglError ret = ggl_file_open(path, O_RDONLY, 0, &fd);
    if (ret != GGL_ERR_OK) {
        GGL_LOGI("Could not open config file.");
        return GGL_ERR_FAILURE;
    }
    GGL_CLEANUP(cleanup_close, fd);

    return ggconfig_load_file_fd(fd);
}

GglError ggconfig_load_dir(GglBuffer path) {
    int config_dir;
    GglError ret = ggl_dir_open(path, O_RDONLY, false, &config_dir);
    if (ret != GGL_ERR_OK) {
        GGL_LOGI("Could not open config directory.");
        return GGL_ERR_FAILURE;
    }

    DIR *dir = fdopendir(config_dir);
    if (dir == NULL) {
        GGL_LOGE("Failed to read config directory.");
        (void) ggl_close(config_dir);
        return GGL_ERR_FAILURE;
    }
    GGL_CLEANUP(cleanup_closedir, dir);

    while (true) {
        // Directory stream is not shared between threads.
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        struct dirent *entry = readdir(dir);
        if (entry == NULL) {
            break;
        }

        if (entry->d_type == DT_REG) {
            int fd = -1;
            ret = ggl_file_openat(
                dirfd(dir),
                ggl_buffer_from_null_term(entry->d_name),
                O_RDONLY,
                0,
                &fd
            );
            if (ret != GGL_ERR_OK) {
                GGL_LOGW("Failed to open config file.");
                break;
            }
            GGL_CLEANUP(cleanup_close, fd);

            (void) ggconfig_load_file_fd(fd);
        }
    }

    return GGL_ERR_OK;
}
