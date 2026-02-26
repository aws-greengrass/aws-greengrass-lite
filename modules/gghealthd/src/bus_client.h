// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGHEALTHD_BUS_H
#define GGHEALTHD_BUS_H

#include <gg/arena.h>
#include <gg/attr.h>
#include <gg/error.h>
#include <gg/types.h>
#include <stdbool.h>

/// use ggconfigd to verify a component's existence
GgError verify_component_exists(GgBuffer component_name);

/// use ggconfigd to list root components
NONNULL(1, 2)
GgError get_root_component_list(GgArena *alloc, GgList *component_names);

/// queries ggconfigd for a component's type and returns true if it is "NUCLEUS"
bool is_nucleus_component_type(GgBuffer component_name);

#endif
