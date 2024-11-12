// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_DEPENDENCY_RESOLVER_H
#define GGDEPLOYMENTD_DEPENDENCY_RESOLVER_H

#include "deployment_handler.h"
#include <ggl/alloc.h>
#include <ggl/error.h>
#include <ggl/object.h>
#include <ggl/vector.h>

GglError resolve_dependencies(
    GglMap *root_components,
    GglBuffer thing_group_name,
    GglDeploymentHandlerThreadArgs *args,
    GglAlloc *alloc,
    GglKVVec *resolved_components_kv_vec
);

#endif
