// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include "ggl/recipe2unit.h"
#include "recipe2unit-test.h"
#include <ggl/bump_alloc.h>
#include <ggl/error.h>
#include <ggl/object.h>
#include <stdint.h>

GglError run_recipe2unit_test(void) {
    Recipe2UnitArgs args = { 0 };
    char recipe_path[] = "./recipe2unit-test/sample/recipe.yml";
    char root_dir[] = ".";

    args.recipe_path = recipe_path;
    args.root_dir = root_dir;
    args.user = "ubuntu";
    args.group = "ubuntu";
    args.recipe_runner_path = "/home/reciperunner";

    GglObject recipe_map;
    GglObject *component_name;
    static uint8_t big_buffer_for_bump[5000];
    GglBumpAlloc bump_alloc = ggl_bump_alloc_init(GGL_BUF(big_buffer_for_bump));

    convert_to_unit(&args, &bump_alloc.alloc, &recipe_map, &component_name);
    return GGL_ERR_OK;
}
