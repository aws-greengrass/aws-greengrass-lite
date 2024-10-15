// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_COMPONENT_STORE_H
#define GGDEPLOYMENTD_COMPONENT_STORE_H

#include <dirent.h>
#include <ggl/buffer.h>
#include <ggl/error.h>

GglError get_recipe_dir_fd(int *recipe_fd);

GglError iterate_over_components(
    DIR *dir,
    GglBuffer *component_name_buffer,
    GglBuffer *version,
    struct dirent **entry
);

GglError find_available_component(
    GglBuffer component_name, GglBuffer requirement, GglBuffer *version
);

#endif
