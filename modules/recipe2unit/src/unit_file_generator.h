// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#ifndef UNIT_FILE_GENERATOR_H
#define UNIT_FILE_GENERATOR_H

#include "ggl/recipe2unit.h"
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/object.h>

typedef enum {
    INSTALL,
    RUN_STARTUP,
    BOOTSTRAP
} PhaseSelection;

GgError generate_systemd_unit(
    GgMap recipe_map,
    GgBuffer *unit_file_buffer,
    Recipe2UnitArgs *args,
    GgObject **component_name,
    PhaseSelection phase
);

#endif
