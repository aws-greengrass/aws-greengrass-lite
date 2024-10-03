// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_COMPONENT_STORE_H
#define GGDEPLOYMENTD_COMPONENT_STORE_H

#include <ggl/alloc.h>
#include <ggl/error.h>
#include <ggl/object.h>

/// Populate a vector with each component name and version saved in the local
/// recipe directory
GglError retrieve_component_list(
    int *out_fd, GglAlloc *alloc, GglMap *component_details
);

GglError find_available_component(
    GglBuffer component_name, GglBuffer requirement, GglBuffer *version
);

#endif
