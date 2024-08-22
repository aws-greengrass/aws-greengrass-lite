// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef FLEET_PROV_DATABASE_HELPER_H
#define FLEET_PROV_DATABASE_HELPER_H

#include <ggl/bump_alloc.h>
#include <ggl/object.h>

void get_value_from_db(
    GglList key_path, GglAlloc *the_allocator, char *return_string
);
GglError save_value_to_db(GglList key_path, GglObject value);
#endif
