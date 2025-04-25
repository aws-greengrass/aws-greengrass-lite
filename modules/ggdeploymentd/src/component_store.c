// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "component_store.h"
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/cleanup.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/log.h>
#include <ggl/semver.h>
#include <limits.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_PATH_LENGTH 128

static GglBuffer root_path = GGL_STR("/var/lib/greengrass");

static GglError update_root_path(void) {
    static uint8_t resp_mem[MAX_PATH_LENGTH] = { 0 };
    GglArena alloc = ggl_arena_init(GGL_BUF(resp_mem));
    GglBuffer resp = { 0 };
    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("rootPath")), &alloc, &resp
    );

    if (ret != GGL_ERR_OK) {
        GGL_LOGW("Failed to get root path from config.");
        if ((ret == GGL_ERR_NOMEM) || (ret == GGL_ERR_FATAL)) {
            return ret;
        }
        return GGL_ERR_OK;
    }

    root_path = resp;
    return GGL_ERR_OK;
}

GglError get_recipe_dir_fd(int *recipe_fd) {
    GglError ret = update_root_path();
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to retrieve root path.");
        return GGL_ERR_FAILURE;
    }

    int root_path_fd;
    ret = ggl_dir_open(root_path, O_PATH, false, &root_path_fd);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to open root_path.");
        return GGL_ERR_FAILURE;
    }
    GGL_CLEANUP(cleanup_close, root_path_fd);

    int recipe_dir_fd;
    ret = ggl_dir_openat(
        root_path_fd,
        GGL_STR("packages/recipes"),
        O_RDONLY,
        false,
        &recipe_dir_fd
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to open recipe subdirectory.");
        return GGL_ERR_FAILURE;
    }
    *recipe_fd = recipe_dir_fd;
    return GGL_ERR_OK;
}

GglError iterate_over_components(
    DIR *dir,
    GglBuffer *component_name_buffer,
    GglBuffer *version,
    struct dirent **entry
) {
    GGL_LOGT("Iterating over component recipes in directory");
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    while ((*entry = readdir(dir)) != NULL) {
        GglBuffer entry_buf = ggl_buffer_from_null_term((*entry)->d_name);
        GGL_LOGT(
            "Found directory entry %.*s", (int) entry_buf.len, entry_buf.data
        );
        // recipe file names follow this format:
        // <component_name>-<version>.<extension>
        // Split the last "-" character to retrieve the component name
        GglBuffer recipe_component;
        GglBuffer rest = GGL_STR("");
        for (size_t i = entry_buf.len; i > 0; --i) {
            if (entry_buf.data[i - 1] == '-') {
                recipe_component = ggl_buffer_substr(entry_buf, 0, i - 1);
                rest = ggl_buffer_substr(entry_buf, i, SIZE_MAX);
                GGL_LOGT(
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
            GGL_LOGD(
                "Recipe file name formatted incorrectly. Continuing to next "
                "file."
            );
            continue;
        }

        // Trim the file extension off the rest. This is the component version.
        GglBuffer recipe_version = GGL_STR("");
        for (size_t i = rest.len; i > 0; i--) {
            if (rest.data[i - 1] == '.') {
                recipe_version = ggl_buffer_substr(rest, 0, i - 1);
                GGL_LOGT(
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
        return GGL_ERR_OK;
    }
    return GGL_ERR_NOENTRY;
}

GglError find_available_component(
    GglBuffer component_name, GglBuffer requirement, GglBuffer *version
) {
    GGL_LOGT(
        "Searching for component %.*s",
        (int) component_name.len,
        component_name.data
    );
    int recipe_dir_fd;
    GglError ret = get_recipe_dir_fd(&recipe_dir_fd);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    // iterate through recipes in the directory
    DIR *dir = fdopendir(recipe_dir_fd);
    if (dir == NULL) {
        GGL_LOGE("Failed to open recipe directory.");
        (void) ggl_close(recipe_dir_fd);
        return GGL_ERR_FAILURE;
    }
    GGL_CLEANUP(cleanup_closedir, dir);

    struct dirent *entry = NULL;
    uint8_t component_name_array[NAME_MAX];
    GglBuffer component_name_buffer
        = { .data = component_name_array, .len = 0 };

    uint8_t version_array[NAME_MAX];
    GglBuffer version_buffer = { .data = version_array, .len = 0 };

    do {
        ret = iterate_over_components(
            dir, &component_name_buffer, &version_buffer, &entry
        );

        if (ret != GGL_ERR_OK) {
            return ret;
        }

        assert(entry != NULL);

        if (ggl_buffer_eq(component_name, component_name_buffer)
            && is_in_range(version_buffer, requirement)) {
            assert(version_buffer.len <= NAME_MAX);
            memcpy(version->data, version_buffer.data, version_buffer.len);
            version->len = version_buffer.len;
            return GGL_ERR_OK;
        }
    } while (true);

    // component meeting version requirements not found
    return GGL_ERR_NOENTRY;
}
