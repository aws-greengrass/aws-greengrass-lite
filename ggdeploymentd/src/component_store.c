// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "component_store.h"
#include <dirent.h>
#include <fcntl.h>
#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/semver.h>
#include <ggl/vector.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_PATH_LENGTH 128

static GglBuffer root_path = GGL_STR("/var/lib/aws-greengrass-v2");

static GglError update_root_path(void) {
    GglMap params = GGL_MAP(
        { GGL_STR("key_path"),
          GGL_OBJ_LIST(GGL_OBJ_STR("system"), GGL_OBJ_STR("rootPath")) }
    );

    static uint8_t resp_mem[MAX_PATH_LENGTH] = { 0 };
    GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(resp_mem));

    GglObject resp;
    GglError ret = ggl_call(
        GGL_STR("/aws/ggl/ggconfigd"),
        GGL_STR("read"),
        params,
        NULL,
        &balloc.alloc,
        &resp
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW("component-store", "Failed to get root path from config.");
        if ((ret == GGL_ERR_NOMEM) || (ret == GGL_ERR_FATAL)) {
            return ret;
        }
        return GGL_ERR_OK;
    }
    if (resp.type != GGL_TYPE_BUF) {
        GGL_LOGE("component-store", "Configuration root path is not a string.");
        return GGL_ERR_INVALID;
    }

    root_path = resp.buf;
    return GGL_ERR_OK;
}

GglError retrieve_component_list(
    int *out_fd, GglAlloc *alloc, GglMap *component_details
) {
    GglError ret = update_root_path();
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("component-store", "Failed to retrieve root path.");
        return GGL_ERR_FAILURE;
    }

    int root_path_fd;
    ret = ggl_dir_open(root_path, O_PATH, false, &root_path_fd);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("component-store", "Failed to open root_path.");
        return GGL_ERR_FAILURE;
    }
    GGL_DEFER(ggl_close, root_path_fd);

    int recipe_dir_fd;
    ret = ggl_dir_openat(
        root_path_fd, GGL_STR("packages/recipes"), O_PATH, false, &recipe_dir_fd
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("component-store", "Failed to open recipe subdirectory.");
        return GGL_ERR_FAILURE;
    }

    // iterate through recipes in the directory
    DIR *dir = fdopendir(recipe_dir_fd);
    if (dir == NULL) {
        GGL_LOGE("component-store", "Failed to open recipe directory.");
        return GGL_ERR_FAILURE;
    }

    GglKV *kv_buffer = GGL_ALLOCN(alloc, GglKV, 256);
    if (!kv_buffer) {
        GGL_LOGE("component-store", "No memory available to allocate.");
        return GGL_ERR_NOMEM;
    }
    GglKVVec components
        = { .map = (GglMap) { .pairs = kv_buffer, .len = 0 }, .capacity = 256 };

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
                "component-store",
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

        // get the substring of the recipe file after the component name and
        // before the file extension. This is the component version
        GglBuffer recipe_version = ggl_buffer_substr(
            GGL_STR(entry->d_name),
            component_name_len,
            strlen(entry->d_name) - file_extension_len
        );

        GglKV component_info
            = { .key = recipe_component, .val = GGL_OBJ(recipe_version) };
        ggl_kv_vec_push(&components, component_info);
    }

    GglObject obj = GGL_OBJ(*component_details);
    ret = ggl_obj_deep_copy(&obj, alloc);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    *component_details = obj.map;
    *out_fd = recipe_dir_fd;
    return GGL_ERR_OK;
}

GglError find_available_component(
    GglBuffer component_name, GglBuffer requirement, GglBuffer *version
) {
    int fd;
    static uint8_t mem[256] = { 0 };
    GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(mem));
    GglMap components;

    GglError ret = retrieve_component_list(&fd, &balloc.alloc, &components);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GGL_MAP_FOREACH(pair, components) {
        if (ggl_buffer_eq(pair->key, component_name)) {
            // if we find the desired component, save the version
            if (pair->val.type != GGL_TYPE_BUF) {
                GGL_LOGE(
                    "component-store",
                    "Component map contains invalid type for version."
                );
                return GGL_ERR_INVALID;
            }
            GglBuffer component_version = pair->val.buf;

            // check if the version satisfies the requirement
            if (is_in_range(component_version, requirement)) {
                *version = component_version;
            }
        }
    }

    return GGL_ERR_OK;
}
