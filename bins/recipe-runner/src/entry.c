// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "recipe-runner.h"
#include "runner.h"
#include <ggl/error.h>

GglError run_recipe_runner(RecipeRunnerArgs *args) {
    GglError ret = runner(args);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return GGL_ERR_OK;
}
