/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RECIPE_RUNNER__H
#define RECIPE_RUNNER__H

#include "recipe-runner.h"
#include <ggl/error.h>

GglError get_file_content(const char *file_path, char *return_value);

GglError runner(const RecipeRunnerArgs *args);

#endif
