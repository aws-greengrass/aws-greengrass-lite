// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_DEPLOYMENT_CONFIGURATION_H
#define GGDEPLOYMENTD_DEPLOYMENT_CONFIGURATION_H

#include <ggl/buffer.h>
#include <ggl/error.h>

typedef struct {
    char data_endpoint[128];
    char cert_path[128];
    char rootca_path[128];
    char pkey_path[128];
    char region[24];
    char port[16];
} DeploymentConfiguration;

extern DeploymentConfiguration config;

/// Read the given key under services -> aws.greengrass.NucleusLite ->
/// configuration -> config_key
GglError read_nucleus_config(GglBuffer config_key, GglBuffer *response);

/// Read the given key under system -> config_key
GglError read_system_config(GglBuffer config_key, GglBuffer *response);

GglError get_posix_user(GglBuffer *posix_user);

#endif
