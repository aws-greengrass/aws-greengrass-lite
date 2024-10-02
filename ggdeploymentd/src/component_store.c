// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "component_store.h"
#include "component_model.h"
#include <sys/types.h>
#include <dirent.h>
#include <fcntl.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/semver.h>
#include <ggl/vector.h>
#include <string.h>
#include <sys/stat.h>
#include <stdint.h>
#include <stdio.h>

#define MAX_PATH_LENGTH 128

static GglBuffer root_path = GGL_STR("/var/lib/aws-greengrass-v2");

GGL_DEFINE_DEFER(closedir, DIR *, dirp, if (*dirp != NULL) closedir(*dirp))

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

void find_available_component(
    GglBuffer component_name,
    GglBuffer requirement,
    ComponentIdentifier *component
) {
    // check /packages/recipes under the root path
    // iterate through all the recipes
    // find a recipe that matches the component name
    // parse that recipe to get the component version

    GglError ret = update_root_path();
    if (ret != GGL_ERR_OK) {
        // we do not error out here because we will negotiate with the cloud if
        // a local component is not found
        GGL_LOGW(
            "component-store",
            "Failed to retrieve root path. Assuming no local component "
            "available."
        );
        return;
    }

    int root_path_fd;
    ret = ggl_dir_open(root_path, O_PATH, &root_path_fd);
    if (ret != GGL_ERR_OK) {
        GGL_LOGW(
            "component-store",
            "Failed to open root_path. Assuming no local component available."
        );
        return;
    }
    GGL_DEFER(close, root_path_fd);

    int recipe_dir_fd;
    ret = ggl_dir_openat(
        root_path_fd, GGL_STR("packages/recipes"), O_PATH, &recipe_dir_fd
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW(
            "component-store",
            "Failed to open recipe subdirectory. Assuming no local component "
            "available."
        );
        return;
    }

    // iterate through recipes in the directory to find the target component, if
    // it exists
    DIR *dir = fdopendir(recipe_dir_fd);
    if (dir == NULL) {
        GGL_LOGW(
            "component-store",
            "Failed to open recipe directory. Assuming no local component "
            "available."
        );
        return;
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
                "component-store",
                "Recipe file name formatted incorrectly. Continuing to next "
                "file."
            );
            continue;
        }

        // copy the component name substring
        GglBuffer recipe_component
            = ggl_buffer_substr(GGL_STR(entry->d_name), 0, component_name_len);

        // if the component names match, save the version of the component
        if (ggl_buffer_eq(component_name, recipe_component)) {
            // find the file extension length
            size_t file_extension_len = 0;
            char *dot_pos = NULL;
            for (size_t i = strlen(entry->d_name) - 1; i >= 0; i--) {
                if (entry->d_name[i] == '.') {
                    dot_pos = entry->d_name + i;
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

            // TODO: check that the recipe version satisfies the requirement
            bool requirement_satisfied = is_contain(recipe_version, requirement);

            if (requirement_satisfied) {
              // save the component information for the caller
              component->name = recipe_component;
              component->version = recipe_version;
            }
        }
    }
}
