// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "gg_fleet_statusd.h"
#include <ggl/error.h>
#include <ggl/version.h>

__attribute__((visibility("default"))) const char *argp_program_version
    = GGL_VERSION;

int main(int argc, char **argv) {
    (void) argc;
    (void) argv;

    GglError ret = run_gg_fleet_statusd();
    if (ret != GGL_ERR_OK) {
        return 1;
    }
}
