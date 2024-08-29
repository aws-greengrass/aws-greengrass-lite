// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef RECIPE_2_UNIT_H
#define RECIPE_2_UNIT_H

#include <ggl/error.h>

typedef struct {
    char *recipe_path;
    char *recipe_runner_path;
    char *thing_name;
    char *aws_region;
    char *ggc_version;
    char *gg_root_ca_path;
    char *socket_path;
    char *aws_container_auth_token;
    char *aws_container_cred_url;
    char *user;
    char *group;
    char *root_dir;
} Recipe2UnitArgs;

GglError convert_to_unit(Recipe2UnitArgs *args);

#endif
