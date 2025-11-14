// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "recipe-runner.h"
#include "runner.h"
#include <gg/error.h>

GgError run_recipe_runner(RecipeRunnerArgs *args) {
    GgError ret = runner(args);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    return GG_ERR_OK;
}
