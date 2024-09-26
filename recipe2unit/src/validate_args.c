// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include "validate_args.h"
#include "ggl/recipe2unit.h"
#include <ggl/error.h>
#include <ggl/log.h>
#include <string.h>

GglError validate_args(Recipe2UnitArgs *args) {
    if (args == NULL) {
        return GGL_ERR_NOENTRY;
    }

    GGL_LOGT("recipe2unit", "recipe_path: %s", args->recipe_path);
    if ((args->recipe_path == NULL) || (strlen(args->recipe_path) == 0)) {
        return GGL_ERR_NOENTRY;
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
    if ((args->root_dir == NULL) || (strlen(args->root_dir) == 0)) {
        return GGL_ERR_NOENTRY;
    }

    return GGL_ERR_OK;
}
