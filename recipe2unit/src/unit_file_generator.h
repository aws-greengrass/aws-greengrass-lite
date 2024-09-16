// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#ifndef UNIT_FILE_GENERATOR_H
#define UNIT_FILE_GENERATOR_H

#include "ggl/recipe2unit.h"
#include <ggl/error.h>
#include <ggl/object.h>

GglError generate_systemd_unit(
    GglMap recipe_map, GglBuffer *unit_file_buffer, Recipe2UnitArgs *args
);

#endif
