// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <argp.h>
#include <gg/log.h>
#include <gg/sdk.h>
#include <ggl/nucleus/init.h>

#ifndef GGL_VERSION
#define GGL_VERSION "0.0.0"
#endif

__attribute__((visibility("default"))) const char *argp_program_version
    = GGL_VERSION;

void ggl_nucleus_init(void) {
    // TODO: Raise rlimits
    GG_LOGI("Nucleus version: %s", GGL_VERSION);
    gg_sdk_init();
}
