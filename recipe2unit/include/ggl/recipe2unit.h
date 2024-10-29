// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef RECIPE_2_UNIT_H
#define RECIPE_2_UNIT_H

#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/object.h>
#include <limits.h>

typedef struct {
    GglBuffer component_name;
    GglBuffer component_version;
    char recipe_runner_path[PATH_MAX];
    char *user;
    char *group;
    char root_dir[PATH_MAX];
    int root_path_fd;
} Recipe2UnitArgs;

GglError convert_to_unit(
    Recipe2UnitArgs *args,
    GglAlloc *alloc,
    GglObject *recipe_obj,
    GglObject **component_name
);

#endif
