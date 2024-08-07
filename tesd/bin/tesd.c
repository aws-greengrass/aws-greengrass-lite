// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "tesd.h"
#include <ggl/error.h>

static char doc[] = "tesd -- Token Exchange Service for AWS credential desperse management";

int main(void) {
    GglError ret = run_tesd();
    if (ret != GGL_ERR_OK) {
        return 1;
    }
}
