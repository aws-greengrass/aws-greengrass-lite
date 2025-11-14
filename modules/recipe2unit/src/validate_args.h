// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#ifndef VALIDATE_ARGS_H
#define VALIDATE_ARGS_H

#include <gg/error.h>
#include <ggl/recipe2unit.h>

GgError validate_args(Recipe2UnitArgs *args);

#endif
