// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// tes-serverd -- A lightweight http server daemon for GGLite

#include "tes-serverd.h"
#include <ggl/error.h>
#include <ggl/version.h>

__attribute__((visibility("default"))) const char *argp_program_version
    = GGL_VERSION;

int main(void) {
    GglError ret = run_tes_serverd();
    if (ret != GGL_ERR_OK) {
        return 1;
    }
}
