// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include "validate_args.h"
#include "ggl/recipe2unit.h"
#include <ggl/error.h>
#include <ggl/log.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>

GglError validate_args(Recipe2UnitArgs *args) {
    if (args == NULL) {
        return GGL_ERR_NOENTRY;
    }

    GGL_LOGT("recipe2unit", "recipe_path: %s", args->recipe_path);
    if (strlen(args->recipe_path) == 0) {
        return GGL_ERR_NOENTRY;
    }
    char resolved_real_path[PATH_MAX] = { 0 };
    if (realpath(args->recipe_path, resolved_real_path) != NULL) {
        memcpy(
            args->recipe_path,
            resolved_real_path,
            strnlen(resolved_real_path, PATH_MAX)
        );
    }

    GGL_LOGT("recipe2unit", "recipe_runner_path: %s", args->recipe_runner_path);
    if (strlen(args->recipe_runner_path) == 0) {
        return GGL_ERR_NOENTRY;
    }
    char resolved_recipe_runner_path[PATH_MAX] = { 0 };
    if (realpath(args->recipe_runner_path, resolved_recipe_runner_path)
        != NULL) {
        memcpy(
            args->recipe_runner_path,
            resolved_recipe_runner_path,
            strnlen(resolved_recipe_runner_path, PATH_MAX)
        );
    }

    GGL_LOGT("recipe2unit", "user: %s", args->user);
    if ((args->user == NULL) || (strlen(args->user) == 0)) {
        return GGL_ERR_NOENTRY;
    }
    GGL_LOGT("recipe2unit", "group: %s", args->group);
    if ((args->group == NULL) || (strlen(args->group) == 0)) {
        return GGL_ERR_NOENTRY;
    }

    GGL_LOGT("recipe2unit", "root_dir: %s", args->root_dir);
    if (strlen(args->root_dir) == 0) {
        return GGL_ERR_NOENTRY;
    }
    char resolved_root_path[PATH_MAX] = { 0 };
    if (realpath(args->root_dir, resolved_root_path) != NULL) {
        memcpy(
            args->root_dir,
            resolved_root_path,
            strnlen(resolved_root_path, PATH_MAX)
        );
    }

    return GGL_ERR_OK;
}
