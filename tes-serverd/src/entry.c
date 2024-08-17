// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "http_server.h"
#include "tes-serverd.h"
#include <ggl/error.h>

GglError run_tes_serverd(void) {
    GglError ret = http_server();
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return GGL_ERR_FAILURE;
}
