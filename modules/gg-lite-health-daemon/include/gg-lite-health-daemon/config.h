// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GG_LITE_HEALTH_DAEMON_CONFIG_H
#define GG_LITE_HEALTH_DAEMON_CONFIG_H

//! Health daemon configuration

#include <gg/error.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_EXCLUDE_ENTRIES 16
#define MAX_EXCLUDE_ENTRY_LEN 64
#define MAX_PATH_LEN 256
#define MAX_LABEL_LEN 16

/// Health daemon configuration values.
typedef struct {
    /// Output format; currently only "emf" is supported.
    char output_mode[MAX_LABEL_LEN];
    /// Directory where EMF metric files are written.
    char output_directory[MAX_PATH_LEN];
    /// Seconds between collection cycles.
    uint32_t collection_interval_sec;
    /// Path to /proc (overridable for containers).
    char proc_path[MAX_PATH_LEN];
    /// Path to /sys (overridable for containers).
    char sys_path[MAX_PATH_LEN];
    /// Filesystem types to skip in disk collection (e.g. "tmpfs").
    char exclude_fstypes[MAX_EXCLUDE_ENTRIES][MAX_EXCLUDE_ENTRY_LEN];
    size_t exclude_fstype_count;
    /// Network interfaces to skip (e.g. "lo").
    char exclude_interfaces[MAX_EXCLUDE_ENTRIES][MAX_EXCLUDE_ENTRY_LEN];
    size_t exclude_interface_count;
} HealthDaemonConfig;

/// Initialize config with defaults.
GgError config_init(HealthDaemonConfig *config);

/// Load config from nucleus core-bus.
GgError config_load(HealthDaemonConfig *config);

#endif
