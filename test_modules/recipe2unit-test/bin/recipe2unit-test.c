// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include "recipe2unit-test.h"
#include <ggl/error.h>

int main(void) {
    GglError ret = run_recipe2unit_test();
    if (ret != GGL_ERR_OK) {
        return 1;
    }
}
