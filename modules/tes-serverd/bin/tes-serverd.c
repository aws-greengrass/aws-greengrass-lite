// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// tes-serverd -- A lightweight http server daemon for GGLite

#include <gg/error.h>
#include <ggl/nucleus/init.h>
#include <tes-serverd.h>

int main(void) {
    ggl_nucleus_init();
    GgError ret = run_tes_serverd();
    if (ret != GG_ERR_OK) {
        return 1;
    }
}
