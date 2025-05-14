// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// tesd -- Token Exchange Service for AWS credential desperse management

#include "tesd.h"
#include <ggl/error.h>
#include <ggl/nucleus/init.h>

int main(void) {
    ggl_nucleus_init();
    GglError ret = run_tesd();
    if (ret != GGL_ERR_OK) {
        return 1;
    }
}
