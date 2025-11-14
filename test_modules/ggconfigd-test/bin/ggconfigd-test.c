// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include <gg/error.h>
#include <ggconfigd-test.h>
#include <ggl/nucleus/init.h>

int main(void) {
    ggl_nucleus_init();
    GgError ret = run_ggconfigd_test();
    if (ret != GG_ERR_OK) {
        return 1;
    }
}
