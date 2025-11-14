// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "http_server.h"
#include <gg/error.h>
#include <tes-serverd.h>

GgError run_tes_serverd(void) {
    GgError ret = http_server();
    if (ret != GG_ERR_OK) {
        return ret;
    }

    return GG_ERR_FAILURE;
}
