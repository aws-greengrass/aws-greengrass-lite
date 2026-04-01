// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// config_load requires a running nucleus (core-bus). Only config_init
// defaults are tested here. Integration tests cover config_load.

#include "gg-lite-health-daemon/config.h"
#include <assert.h>
#include <gg/error.h>
#include <string.h>

static void test_config_init_defaults(void) {
    HealthDaemonConfig config;
    assert(config_init(&config) == GG_ERR_OK);

    assert(strcmp(config.output_mode, "emf") == 0);
    assert(
        strcmp(config.output_directory, "/var/log/gg-metrics/system-health/")
        == 0
    );
    assert(config.collection_interval_sec == 60);
    assert(strcmp(config.proc_path, "/proc") == 0);
    assert(strcmp(config.sys_path, "/sys") == 0);

    assert(config.exclude_fstype_count == 4);
    assert(strcmp(config.exclude_fstypes[0], "tmpfs") == 0);
    assert(strcmp(config.exclude_fstypes[1], "proc") == 0);
    assert(strcmp(config.exclude_fstypes[2], "sysfs") == 0);
    assert(strcmp(config.exclude_fstypes[3], "devtmpfs") == 0);

    assert(config.exclude_interface_count == 1);
    assert(strcmp(config.exclude_interfaces[0], "lo") == 0);
}

int main(void) {
    test_config_init_defaults();
    return 0;
}
