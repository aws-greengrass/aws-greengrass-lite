// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_MAP_H
#define GGL_MAP_H

//! Map utilities

#include "error.h"
#include "object.h"
#include <ggl/buffer.h>
#include <stdbool.h>
#include <stddef.h>

// NOLINTBEGIN(bugprone-macro-parentheses)
/// Loop over the KV pairs in a map.
#define GGL_MAP_FOREACH(name, map) \
    for (GglKV *name = (map).pairs; name < &(map).pairs[(map).len]; \
         name = &name[1])
// NOLINTEND(bugprone-macro-parentheses)

/// Get the value corresponding with a key.
/// Returns whether the key was found in the map.
/// If `result` is not NULL it is set to the found value or NULL.
bool ggl_map_get(GglMap map, GglBuffer key, GglObject **result);

typedef struct {
    GglBuffer key;
    bool required;
    GglObjectType type;
    GglObject **value;
} GglMapSchemaEntry;

typedef struct {
    GglMapSchemaEntry *entries;
    size_t entry_count;
} GglMapSchema;

#define GGL_MAP_SCHEMA(...) \
    (GglMapSchema) { \
        .entries = (GglMapSchemaEntry[]) { __VA_ARGS__ }, \
        .entry_count = (sizeof((GglMapSchemaEntry[]) { __VA_ARGS__ })) \
            / (sizeof(GglMapSchemaEntry)) \
    }

GglError ggl_map_validate(GglMap map, GglMapSchema schema);

#endif
