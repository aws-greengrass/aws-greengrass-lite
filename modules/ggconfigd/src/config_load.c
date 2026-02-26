// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "helpers.h"
#include <dirent.h>
#include <fcntl.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/file.h>
#include <gg/log.h>
#include <gg/object.h>
#include <gg/types.h>
#include <gg/vector.h>
#include <ggconfigd.h>
#include <ggl/yaml_decode.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

static GgError ggconfig_load_file_fd(int fd) {
    static uint8_t file_mem[8192];
    GgBuffer config_file = GG_BUF(file_mem);

    GgError ret = gg_file_read(fd, &config_file);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to read config file.");
        return GG_ERR_FAILURE;
    }

    static uint8_t decode_mem[500 * sizeof(GgObject)];
    GgArena alloc = gg_arena_init(GG_BUF(decode_mem));

    GgObject config_obj;
    ret = ggl_yaml_decode_destructive(config_file, &alloc, &config_obj);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to parse config file.");
        return GG_ERR_FAILURE;
    }

    GgObjVec key_path = GG_OBJ_VEC((GgObject[GG_MAX_OBJECT_DEPTH]) { 0 });

    GG_LOGD(
        "Processing file load merge to key %s with timestamp 2",
        print_key_path(&key_path.list)
    );

    if (gg_obj_type(config_obj) == GG_TYPE_MAP) {
        ret = ggconfig_process_map(&key_path, gg_obj_into_map(config_obj), 2);
        if (ret != GG_ERR_OK) {
            return ret;
        }
    } else {
        ret = ggconfig_process_nonmap(&key_path, config_obj, 2);
        if (ret != GG_ERR_OK) {
            return ret;
        }
    }

    return GG_ERR_OK;
}

GgError ggconfig_load_file(GgBuffer path) {
    GG_LOGT("Loading file %.*s", (int) path.len, path.data);
    int fd;
    GgError ret = gg_file_open(path, O_RDONLY, 0, &fd);
    if (ret != GG_ERR_OK) {
        GG_LOGI("Could not open config file.");
        return GG_ERR_FAILURE;
    }
    GG_CLEANUP(cleanup_close, fd);

    return ggconfig_load_file_fd(fd);
}

GgError ggconfig_load_dir(GgBuffer path) {
    GG_LOGT(
        "Loading files from config directory %.*s", (int) path.len, path.data
    );
    int config_dir;
    GgError ret = gg_dir_open(path, O_RDONLY, false, &config_dir);
    if (ret != GG_ERR_OK) {
        GG_LOGI("Could not open config directory.");
        return GG_ERR_FAILURE;
    }

    DIR *dir = fdopendir(config_dir);
    if (dir == NULL) {
        GG_LOGE("Failed to read config directory.");
        (void) gg_close(config_dir);
        return GG_ERR_FAILURE;
    }
    GG_CLEANUP(cleanup_closedir, dir);

    while (true) {
        // Directory stream is not shared between threads.
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        struct dirent *entry = readdir(dir);
        if (entry == NULL) {
            break;
        }

        if (entry->d_type == DT_REG) {
            GG_LOGT("Loading directory file %s", entry->d_name);

            int fd = -1;
            ret = gg_file_openat(
                dirfd(dir),
                gg_buffer_from_null_term(entry->d_name),
                O_RDONLY,
                0,
                &fd
            );
            if (ret != GG_ERR_OK) {
                GG_LOGW("Failed to open config file.");
                break;
            }
            GG_CLEANUP(cleanup_close, fd);

            (void) ggconfig_load_file_fd(fd);
        }
    }

    return GG_ERR_OK;
}
