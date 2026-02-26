// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_RECIPE_H
#define GGL_RECIPE_H

//! Greengrass recipe utils

#include <gg/arena.h>
#include <gg/error.h>
#include <gg/types.h>
#include <stdbool.h>

typedef struct GglRecipeVariable {
    GgBuffer component_dependency_name;
    GgBuffer type;
    GgBuffer key;
} GglRecipeVariable;

GgError ggl_recipe_get_from_file(
    int root_path_fd,
    GgBuffer component_name,
    GgBuffer component_version,
    GgArena *arena,
    GgObject *recipe
);

GgError fetch_script_section(
    GgMap selected_lifecycle,
    GgBuffer selected_phase,
    bool *is_root,
    GgBuffer *out_selected_script_as_buf,
    GgMap *out_set_env_as_map,
    GgBuffer *out_timeout_value
);

GgError select_linux_lifecycle(
    GgMap recipe_map, GgMap *out_selected_lifecycle_map
);
GgError select_linux_manifest(
    GgMap recipe_map, GgMap *out_selected_linux_manifest
);

GgBuffer get_current_architecture(void);

GgError ggl_get_recipe_artifacts_for_platform(
    GgMap recipe_map, GgList *out_platform_artifacts
);

/// Returns true if the given string is a recipe variable
/// e.g. GG_STR("{configuration:/version}")
bool ggl_is_recipe_variable(GgBuffer str);

/// Parses a string into a recipe variable without modifying it.
/// The output will contain substrings of the input string on success.
GgError ggl_parse_recipe_variable(
    GgBuffer str, GglRecipeVariable *out_variable
);

#endif
