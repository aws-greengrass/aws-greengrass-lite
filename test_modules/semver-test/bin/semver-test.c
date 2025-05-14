// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include "semver-test.h"
#include <ggl/error.h>
#include <ggl/nucleus/init.h>

int main(void) {
    ggl_nucleus_init();

    GglError ret = run_semver_test();
    if (ret != GGL_ERR_OK) {
        return 1;
    }
}
