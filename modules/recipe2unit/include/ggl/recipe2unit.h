// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef RECIPE_2_UNIT_H
#define RECIPE_2_UNIT_H

#include <gg/arena.h>
#include <gg/error.h>
#include <gg/types.h>
#include <limits.h>
#include <stdbool.h>

typedef struct {
    bool has_install;
    bool has_run_startup;
    bool has_bootstrap;
    /// Whether generating the units changed any unit file that was already on
    /// disk, or wrote one that was not there before. A revision that alters
    /// something a unit encodes only takes effect once the component is
    /// restarted, so a caller that skips redeployment for an unchanged
    /// component version needs this to tell the two cases apart.
    bool unit_changed;
} HasPhase;

typedef struct {
    GgBuffer component_name;
    GgBuffer component_version;
    char recipe_runner_path[PATH_MAX];
    const char *user;
    const char *group;
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
/// @param[out] existing_phases Status of which phases are present, and whether
/// any unit file's content changed
/// @return GG_ERR_OK on success. Failure otherwise.
GgError convert_to_unit(
    Recipe2UnitArgs *args,
    GgArena *alloc,
    GgObject *recipe_obj,
    GgObject **component_name,
    HasPhase *existing_phases
);

#endif
