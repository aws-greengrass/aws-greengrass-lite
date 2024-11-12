// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_DEPLOYMENT_CONFIGURATION_H
#define GGDEPLOYMENTD_DEPLOYMENT_CONFIGURATION_H

#include <ggl/error.h>
#include <ggl/vector.h>

typedef struct {
    char data_endpoint[128];
    char cert_path[128];
    char rootca_path[128];
    char pkey_path[128];
    char region[24];
    char port[16];
} DeploymentConfiguration;

extern DeploymentConfiguration config;

GglError get_data_endpoint(GglByteVec *endpoint);
GglError get_data_port(GglByteVec *port);
GglError get_region(GglByteVec *region);
GglError get_thing_name(char **thing_name);
GglError get_root_ca_path(char **root_ca_path);
GglError get_tes_cred_url(char **tes_cred_url);
GglError get_posix_user(char **posix_user);
GglError get_private_key_path(GglByteVec *pkey_path);
GglError get_cert_path(GglByteVec *cert_path);
GglError get_rootca_path(GglByteVec *rootca_path);

#endif
