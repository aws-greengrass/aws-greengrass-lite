// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef RECIPE_2_UNIT_H
#define RECIPE_2_UNIT_H

#include <ggl/bump_alloc.h>
#include <ggl/error.h>
#include <ggl/object.h>

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

GglError get_recipe_obj(
    Recipe2UnitArgs *args, GglBumpAlloc *balloc, GglObject *recipe_obj
);
GglError convert_to_unit(Recipe2UnitArgs *args);

#endif
