// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "component_store.h"
#include <dirent.h>
#include <fcntl.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/semver.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_PATH_LENGTH 128

static GglBuffer root_path = GGL_STR("/var/lib/aws-greengrass-v2");

static GglError update_root_path(void) {
    static uint8_t resp_mem[MAX_PATH_LENGTH] = { 0 };
    GglBuffer resp = GGL_BUF(resp_mem);
    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("rootPath")), &resp
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

static GglError get_recipe_dir_fd(int *recipe_fd) {
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
    GGL_DEFER(ggl_close, root_path_fd);

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

GglError find_available_component(
    GglBuffer component_name, GglBuffer requirement, GglBuffer *version
) {
    int recipe_dir_fd;
    GglError ret = get_recipe_dir_fd(&recipe_dir_fd);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    // iterate through recipes in the directory
    DIR *dir = fdopendir(recipe_dir_fd);
    if (dir == NULL) {
        GGL_LOGE("Failed to open recipe directory.");
        return GGL_ERR_FAILURE;
    }

    struct dirent *entry;
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    while ((entry = readdir(dir)) != NULL) {
        // recipe file names follow the format component_name-version.
        // concatenate to the component name and compare with the target
        // component name Find the index of the "-" character
        char *dash_pos = entry->d_name;
        size_t component_name_len = 0;
        while (*dash_pos != '\0' && *dash_pos != '-') {
            dash_pos++;
            component_name_len++;
        }
        if (*dash_pos != '-') {
            GGL_LOGW(
                "Recipe file name formatted incorrectly. Continuing to next "
                "file."
            );
            continue;
        }

        // copy the component name substring
        GglBuffer recipe_component
            = ggl_buffer_substr(GGL_STR(entry->d_name), 0, component_name_len);

        // find the file extension length
        size_t file_extension_len = 0;
        char *dot_pos = NULL;
        for (size_t i = strlen(entry->d_name); i > 0; i--) {
            if (entry->d_name[i - 1] == '.') {
                dot_pos = entry->d_name + i - 1;
                file_extension_len = strlen(dot_pos + 1);
                break;
            }
        }
        // account for '.'
        file_extension_len += 1;

        // get the substring of the recipe file after the component name and
        // before the file extension. This is the component version
        GglBuffer recipe_version = ggl_buffer_substr(
            GGL_STR(entry->d_name),
            component_name_len + 1,
            strlen(entry->d_name) - file_extension_len
        );

        if (ggl_buffer_eq(component_name, recipe_component)
            && is_in_range(recipe_version, requirement)) {
            *version = recipe_version;
        }
    }

    return GGL_ERR_OK;
}
