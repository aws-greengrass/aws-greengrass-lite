// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_RECIPE_H
#define GGL_RECIPE_H

//! Greengrass recipe utils

#include "stdbool.h"
#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/object.h>

typedef struct GglRecipeVariable {
    GglBuffer component_dependency_name;
    GglBuffer type;
    GglBuffer key;
} GglRecipeVariable;

GglError ggl_recipe_get_from_file(
    int root_path_fd,
    GglBuffer component_name,
    GglBuffer component_version,
    GglArena *arena,
    GglObject *recipe
);

GglError fetch_script_section(
    GglMap selected_lifecycle,
    GglBuffer selected_phase,
    bool *is_root,
    GglBuffer *out_selected_script_as_buf,
    GglMap *out_set_env_as_map,
    GglBuffer *out_timeout_value
);

GglError select_linux_lifecycle(
    GglMap recipe_map, GglMap *out_selected_lifecycle_map
);
GglError select_linux_manifest(
    GglMap recipe_map, GglMap *out_selected_linux_manifest
);

GglBuffer get_current_architecture(void);

/// Returns true if the given string is a recipe variable
/// e.g. GGL_STR("{configuration:/version}")
bool ggl_is_recipe_variable(GglBuffer str);

/// Parses a string into a recipe variable without modifying it.
/// The output will contain substrings of the input string on success.
GglError ggl_parse_recipe_variable(
    GglBuffer str, GglRecipeVariable *out_variable
);

#endif
