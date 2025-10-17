// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGHEALTHD_BUS_H
#define GGHEALTHD_BUS_H

#include <ggl/arena.h>
#include <ggl/attr.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/object.h>
#include <stdbool.h>

/// use ggconfigd to verify a component's existence
GglError verify_component_exists(GglBuffer component_name);

/// use ggconfigd to list root components
NONNULL(1, 2)
GglError get_root_component_list(GglArena *alloc, GglList *component_names);

/// queries ggconfigd for a component's type and returns true if it is "NUCLEUS"
bool is_nucleus_component_type(GglBuffer component_name);

#endif
