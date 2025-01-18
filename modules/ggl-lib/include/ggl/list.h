// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_LIST_H
#define GGL_LIST_H

//! List utilities

#include "error.h"
#include "object.h"

// NOLINTBEGIN(bugprone-macro-parentheses)
/// Loop over the objects in a list.
#define GGL_LIST_FOREACH(name, list) \
    for (GglObject *name = (list).items; name < &(list).items[(list).len]; \
         name = &name[1])
// NOLINTEND(bugprone-macro-parentheses)

GglError ggl_list_type_check(GglList list, GglObjectType type);

#endif
