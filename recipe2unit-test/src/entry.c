// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include "ggl/recipe2unit.h"
#include "recipe2unit-test.h"
#include <ggl/error.h>

GglError run_recipe2unit_test(void) {
    Recipe2UnitArgs args = { 0 };
    char recipe_path[] = "./recipe2unit_python/recipe.yml";
    char root_dir[] = ".";

    args.recipe_path = recipe_path;
    args.root_dir = root_dir;

    convert_to_unit(&args);
    return GGL_ERR_OK;
}
