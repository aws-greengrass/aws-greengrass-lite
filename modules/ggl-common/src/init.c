// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <argp.h>
#include <ggl/nucleus/init.h>
#include <ggl/sdk.h>

#ifndef GGL_VERSION
#define GGL_VERSION "0.0.0"
#endif

__attribute__((visibility("default"))) const char *argp_program_version
    = GGL_VERSION;

void ggl_nucleus_init(void) {
    // TODO: Raise rlimits
    ggl_sdk_init();
}
