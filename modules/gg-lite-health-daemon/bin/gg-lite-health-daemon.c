// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <gg/error.h>
#include <gg_lite_health_daemon.h>
#include <ggl/nucleus/init.h>

int main(int argc, char **argv) {
    (void) argc;
    (void) argv;

    ggl_nucleus_init();

    GgError ret = run_gg_lite_health_daemon();
    if (ret != GG_ERR_OK) {
        return 1;
    }
}
