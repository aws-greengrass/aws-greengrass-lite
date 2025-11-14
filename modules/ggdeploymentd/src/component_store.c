// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "component_store.h"
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/file.h>
#include <gg/log.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/semver.h>
#include <limits.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_PATH_LENGTH 128

static GgBuffer root_path = GG_STR("/var/lib/greengrass");

static GgError update_root_path(void) {
    static uint8_t resp_mem[MAX_PATH_LENGTH] = { 0 };
    GgArena alloc = gg_arena_init(GG_BUF(resp_mem));
    GgBuffer resp = { 0 };
    GgError ret = ggl_gg_config_read_str(
        GG_BUF_LIST(GG_STR("system"), GG_STR("rootPath")), &alloc, &resp
    );

    if (ret != GG_ERR_OK) {
        GG_LOGW("Failed to get root path from config.");
        if ((ret == GG_ERR_NOMEM) || (ret == GG_ERR_FATAL)) {
            return ret;
        }
        return GG_ERR_OK;
    }

    root_path = resp;
    return GG_ERR_OK;
}

GgError get_recipe_dir_fd(int *recipe_fd) {
    GgError ret = update_root_path();
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to retrieve root path.");
        return GG_ERR_FAILURE;
    }

    int root_path_fd;
    ret = gg_dir_open(root_path, O_PATH, false, &root_path_fd);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to open root_path.");
        return GG_ERR_FAILURE;
    }
    GG_CLEANUP(cleanup_close, root_path_fd);

    int recipe_dir_fd;
    ret = gg_dir_openat(
        root_path_fd,
        GG_STR("packages/recipes"),
        O_RDONLY,
        false,
        &recipe_dir_fd
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to open recipe subdirectory.");
        return GG_ERR_FAILURE;
    }
    *recipe_fd = recipe_dir_fd;
    return GG_ERR_OK;
}

GgError iterate_over_components(
    DIR *dir,
    GgBuffer *component_name_buffer,
    GgBuffer *version,
    struct dirent **entry
) {
    GG_LOGT("Iterating over component recipes in directory");
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    while ((*entry = readdir(dir)) != NULL) {
        GgBuffer entry_buf = gg_buffer_from_null_term((*entry)->d_name);
        GG_LOGT(
            "Found directory entry %.*s", (int) entry_buf.len, entry_buf.data
        );
        // recipe file names follow this format:
        // <component_name>-<version>.<extension>
        // Split the last "-" character to retrieve the component name
        GgBuffer recipe_component;
        GgBuffer rest = GG_STR("");
        for (size_t i = entry_buf.len; i > 0; --i) {
            if (entry_buf.data[i - 1] == '-') {
                recipe_component = gg_buffer_substr(entry_buf, 0, i - 1);
                rest = gg_buffer_substr(entry_buf, i, SIZE_MAX);
                GG_LOGT(
                    "Split entry on '-': component: %.*s rest: %.*s",
                    (int) recipe_component.len,
                    recipe_component.data,
                    (int) rest.len,
                    rest.data
                );
                break;
            }
        }
        if (rest.len == 0) {
            GG_LOGD(
                "Recipe file name formatted incorrectly. Continuing to next file."
            );
            continue;
        }

        // Trim the file extension off the rest. This is the component version.
        GgBuffer recipe_version = GG_STR("");
        for (size_t i = rest.len; i > 0; i--) {
            if (rest.data[i - 1] == '.') {
                recipe_version = gg_buffer_substr(rest, 0, i - 1);
                GG_LOGT(
                    "Found version: %.*s",
                    (int) recipe_version.len,
                    recipe_version.data
                );
                break;
            }
        }

        assert(recipe_component.len < NAME_MAX);
        assert(recipe_version.len < NAME_MAX);
        // Copy out component name and version.
        memcpy(
            component_name_buffer->data,
            recipe_component.data,
            recipe_component.len
        );
        component_name_buffer->len = recipe_component.len;

        memcpy(version->data, recipe_version.data, recipe_version.len);
        version->len = recipe_version.len;

        // Found one component. Break out of loop and return.
        return GG_ERR_OK;
    }
    return GG_ERR_NOENTRY;
}

GgError find_available_component(
    GgBuffer component_name, GgBuffer requirement, GgBuffer *version
) {
    GG_LOGT(
        "Searching for component %.*s",
        (int) component_name.len,
        component_name.data
    );
    int recipe_dir_fd;
    GgError ret = get_recipe_dir_fd(&recipe_dir_fd);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    // iterate through recipes in the directory
    DIR *dir = fdopendir(recipe_dir_fd);
    if (dir == NULL) {
        GG_LOGE("Failed to open recipe directory.");
        (void) gg_close(recipe_dir_fd);
        return GG_ERR_FAILURE;
    }
    GG_CLEANUP(cleanup_closedir, dir);

    struct dirent *entry = NULL;
    uint8_t component_name_array[NAME_MAX];
    GgBuffer component_name_buffer = { .data = component_name_array, .len = 0 };

    uint8_t version_array[NAME_MAX];
    GgBuffer version_buffer = { .data = version_array, .len = 0 };

    do {
        ret = iterate_over_components(
            dir, &component_name_buffer, &version_buffer, &entry
        );

        if (ret != GG_ERR_OK) {
            return ret;
        }

        assert(entry != NULL);

        if (gg_buffer_eq(component_name, component_name_buffer)
            && is_in_range(version_buffer, requirement)) {
            assert(version_buffer.len <= NAME_MAX);
            memcpy(version->data, version_buffer.data, version_buffer.len);
            version->len = version_buffer.len;
            return GG_ERR_OK;
        }
    } while (true);

    // component meeting version requirements not found
    return GG_ERR_NOENTRY;
}
