// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_COMPONENT_STORE_H
#define GGDEPLOYMENTD_COMPONENT_STORE_H

#include <dirent.h>
#include <gg/error.h>
#include <gg/types.h>

GgError get_recipe_dir_fd(int *recipe_fd);

GgError iterate_over_components(
    DIR *dir,
    GgBuffer *component_name_buffer,
    GgBuffer *version,
    struct dirent **entry
);

GgError find_available_component(
    GgBuffer component_name, GgBuffer requirement, GgBuffer *version
);

#endif
