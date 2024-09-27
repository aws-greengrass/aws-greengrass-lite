// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef RECIPE_2_UNIT_H
#define RECIPE_2_UNIT_H

#include <ggl/alloc.h>
#include <ggl/error.h>
#include <ggl/object.h>
#include <linux/limits.h>

typedef struct {
    char recipe_path[PATH_MAX];
    char recipe_runner_path[PATH_MAX];
    char *user;
    char *group;
    char root_dir[PATH_MAX];
} Recipe2UnitArgs;

GglError get_recipe_obj(
    Recipe2UnitArgs *args, GglAlloc *alloc, GglObject *recipe_obj
);

GglError convert_to_unit(
    Recipe2UnitArgs *args,
    GglAlloc *alloc,
    GglObject *recipe_obj,
    GglObject **component_name
);

#endif
