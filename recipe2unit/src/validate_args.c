// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include "validate_args.h"
#include "ggl/recipe2unit.h"
#include <ggl/error.h>
#include <ggl/log.h>
#include <string.h>

GglError validate_args(Recipe2UnitArgs *args) {
    if (args == NULL) {
        return GGL_ERR_NOENTRY;
    }

    GGL_LOGT(
        "recipe2unit",
        "AWS Container auth token: %s",
        args->aws_container_auth_token
    );
    if ((args->aws_container_auth_token == NULL)
        || (strlen(args->aws_container_auth_token) == 0)) {
        return GGL_ERR_NOENTRY;
    }

    GGL_LOGT(
        "recipe2unit",
        "aws_container_cred_url: %s",
        args->aws_container_cred_url
    );
    if ((args->aws_container_auth_token == NULL)
        || (strlen(args->aws_container_auth_token) == 0)) {
        return GGL_ERR_NOENTRY;
    }

    GGL_LOGT("recipe2unit", "aws_region: %s", args->aws_region);
    if ((args->aws_region == NULL) || (strlen(args->aws_region) == 0)) {
        return GGL_ERR_NOENTRY;
    }

    GGL_LOGT("recipe2unit", "gg_root_ca_path: %s", args->gg_root_ca_path);
    if ((args->gg_root_ca_path == NULL)
        || (strlen(args->gg_root_ca_path) == 0)) {
        return GGL_ERR_NOENTRY;
    }

    GGL_LOGT("recipe2unit", "ggc_version: %s", args->ggc_version);
    if ((args->ggc_version == NULL) || (strlen(args->ggc_version) == 0)) {
        return GGL_ERR_NOENTRY;
    }

    GGL_LOGT("recipe2unit", "recipe_path: %s", args->recipe_path);
    if ((args->recipe_path == NULL) || (strlen(args->recipe_path) == 0)) {
        return GGL_ERR_NOENTRY;
    }

    GGL_LOGT("recipe2unit", "thing_name: %s", args->thing_name);
    if ((args->thing_name == NULL) || (strlen(args->thing_name) == 0)) {
        return GGL_ERR_NOENTRY;
    }

    GGL_LOGT("recipe2unit", "socket_path: %s", args->socket_path);
    if ((args->socket_path == NULL) || (strlen(args->socket_path) == 0)) {
        return GGL_ERR_NOENTRY;
    }

    GGL_LOGT("recipe2unit", "user: %s", args->user);
    if ((args->user == NULL) || (strlen(args->user) == 0)) {
        return GGL_ERR_NOENTRY;
    }
    GGL_LOGT("recipe2unit", "group: %s", args->group);
    if ((args->group == NULL) || (strlen(args->group) == 0)) {
        return GGL_ERR_NOENTRY;
    }

    GGL_LOGT("recipe2unit", "root_dir: %s", args->root_dir);
    if ((args->root_dir == NULL) || (strlen(args->root_dir) == 0)) {
        return GGL_ERR_NOENTRY;
    }

    return GGL_ERR_OK;
}
