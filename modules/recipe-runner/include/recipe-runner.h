// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef RECIPE_RUNNER_H
#define RECIPE_RUNNER_H

#include <ggl/error.h>

typedef struct {
    char *component_name;
    char *component_version;
    char *phase;
} RecipeRunnerArgs;

GglError run_recipe_runner(RecipeRunnerArgs *args);

#endif
