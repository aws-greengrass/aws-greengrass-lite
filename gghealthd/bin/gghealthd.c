// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "bus_server.h"
#include <ggl/error.h>

int main(void) {
    GglError ret = run_gghealthd();
    if (ret != GGL_ERR_OK) {
        return 1;
    }
}
