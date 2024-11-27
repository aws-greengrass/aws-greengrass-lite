// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_RECIPE_H
#define GGL_RECIPE_H

//! Greengrass recipe utils

#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/object.h>

GglError ggl_recipe_get_from_file(
    int root_path,
    GglBuffer component_name,
    GglBuffer component_version,
    GglAlloc *alloc,
    GglObject *recipe
);

GglError select_linux_manifest(
    GglMap recipe_map, GglMap *out_selected_lifecycle_map
);

#endif
