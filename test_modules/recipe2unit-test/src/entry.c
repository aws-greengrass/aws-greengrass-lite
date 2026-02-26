// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include <fcntl.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/file.h>
#include <gg/log.h>
#include <gg/types.h>
#include <ggl/recipe2unit.h>
#include <recipe2unit-test.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

// For the testing purpose, move the sample recipe.yml to /run/packages/recipes
// and rename it to recipe-1.0.0.yml

GgError run_recipe2unit_test(void) {
    static Recipe2UnitArgs args = { 0 };
    GgBuffer root_dir = GG_STR(".");
    GgBuffer recipe_runner_path = GG_STR("[Path to recipe runner here]");

    int root_path_fd;
    GgError ret = gg_dir_open(root_dir, O_PATH, false, &root_path_fd);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to open root dir.");
        return ret;
    }
    args.root_path_fd = root_path_fd;

    memcpy(args.root_dir, root_dir.data, root_dir.len + 1);
    args.user = "ubuntu";
    args.group = "ubuntu";
    memcpy(
        args.recipe_runner_path,
        recipe_runner_path.data,
        recipe_runner_path.len + 1
    );
    args.component_name = GG_STR("[Component Name here]");
    args.component_version = GG_STR("[Component Version here]");

    GgObject recipe_map;
    GgObject *component_name_obj;
    static uint8_t big_buffer_for_bump[50000];
    GgArena alloc = gg_arena_init(GG_BUF(big_buffer_for_bump));
    HasPhase phases = { 0 };

    return convert_to_unit(
        &args, &alloc, &recipe_map, &component_name_obj, &phases
    );
}
