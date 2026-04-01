// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Verify the daemon entry point compiles and links.

#include "gg-lite-health-daemon/config.h"
#include <assert.h>
#include <gg/error.h>
#include <stdio.h>

static void test_config_linkage(void) {
    HealthDaemonConfig config;
    assert(config_init(&config) == GG_ERR_OK);
    assert(config.collection_interval_sec == 60);
}

int main(void) {
    test_config_linkage();
    printf("test_main: all tests passed\n");
    return 0;
}
