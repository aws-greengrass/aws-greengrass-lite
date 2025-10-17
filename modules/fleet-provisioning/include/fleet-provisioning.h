// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef FLEET_PROVISIONING_H
#define FLEET_PROVISIONING_H

#include <ggl/error.h>

typedef struct {
    char *claim_cert;
    char *claim_key;
    char *template_name;
    char *template_params;
    char *endpoint;
    char *root_ca_path;
    char *iotcored_path;
    char *output_dir;
} FleetProvArgs;

GglError run_fleet_prov(FleetProvArgs *args);
#endif
