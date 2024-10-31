// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include "ggl/recipe2unit.h"
#include "recipe2unit-test.h"
#include <fcntl.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

// For the testing purpose, move the sample recipe.yml to /run/packages/recipes
// and rename it to recipe-1.0.0.yml

GglError run_recipe2unit_test(void) {
    static Recipe2UnitArgs args = { 0 };
    char component_name[] = "recipe";
    char version[] = "1.0.0";
    char root_dir[] = ".";
    char recipe_runner_path[] = "/home/reciperunner";

    int root_path_fd;
    GglError ret
        = ggl_dir_open(GGL_STR(root_dir), O_PATH, false, &root_path_fd);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to open root dir.");
        return ret;
    }
    args.root_path_fd = root_path_fd;

    GglBuffer component_name_buff
        = (GglBuffer) { (uint8_t *) component_name, strlen(component_name) };
    GglBuffer version_buff
        = (GglBuffer) { (uint8_t *) version, strlen(version) };
    memcpy(args.root_dir, root_dir, strlen(root_dir));
    args.user = "ubuntu";
    args.group = "ubuntu";
    memcpy(
        args.recipe_runner_path, recipe_runner_path, strlen(recipe_runner_path)
    );
    args.component_name = component_name_buff;
    args.component_version = version_buff;

    GglObject recipe_map;
    GglObject *component_name_obj;
    static uint8_t big_buffer_for_bump[5000];
    GglBumpAlloc bump_alloc = ggl_bump_alloc_init(GGL_BUF(big_buffer_for_bump));

    convert_to_unit(&args, &bump_alloc.alloc, &recipe_map, &component_name_obj);
    return GGL_ERR_OK;
}
