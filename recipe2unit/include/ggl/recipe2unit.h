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
#include <stdbool.h>

typedef struct {
    bool has_install;
    bool has_run_startup;
    bool has_bootstrap;
} HasPhase;

typedef struct {
    GglBuffer component_name;
    GglBuffer component_version;
    char recipe_runner_path[PATH_MAX];
    char *user;
    char *group;
    char root_dir[PATH_MAX];
    int root_path_fd;
} Recipe2UnitArgs;

/// @brief Convert a given recipe file to
/// @param[in] args Recipe2Unit arguments
/// @param[in] alloc allocator interface which is used to create the recipe
/// object which is then copied to the object pointed to by #recipe_obj.
/// @param[out] recipe_obj The object containing the recipe in a map format
/// @param[out] component_name The name of the component as provided by the
/// recipe
/// @param[out] existing_phases Status of which phases are present
/// @return GGL_ERR_OK on success. Failure otherwise.
GglError convert_to_unit(
    Recipe2UnitArgs *args,
    GglAlloc *alloc,
    GglObject *recipe_obj,
    GglObject **component_name,
    HasPhase *existing_phases
);

#endif
