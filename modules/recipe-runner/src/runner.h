/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RUNNER_H
#define RUNNER_H

#include "recipe-runner.h"
#include <gg/error.h>

GgError get_file_content(const char *file_path, char *return_value);

GgError runner(const RecipeRunnerArgs *args);

#endif
