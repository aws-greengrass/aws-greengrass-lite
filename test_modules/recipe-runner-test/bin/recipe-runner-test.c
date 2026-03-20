// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include <gg/error.h>
#include <ggl/nucleus/init.h>
#include <recipe-runner-test.h>

int main(void) {
    ggl_nucleus_init();
    GgError ret = run_recipe_runner_test();
    if (ret != GG_ERR_OK) {
        return 1;
    }
}
