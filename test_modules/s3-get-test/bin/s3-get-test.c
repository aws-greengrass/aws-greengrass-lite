// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "s3-get-test.h"
#include <ggl/error.h>
#include <ggl/nucleus/init.h>

int main(int argc, char **argv) {
    if (argc < 4) {
        return 1;
    }

    ggl_nucleus_init();

    // resuse key for file_path if not provided
    char *file_path = (argc < 5) ? argv[3] : argv[4];
    GglError ret = run_s3_test(argv[1], argv[2], argv[3], file_path);
    if (ret != GGL_ERR_OK) {
        return 1;
    }
}
