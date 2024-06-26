/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_MAP_H
#define GGL_MAP_H

/*! Map utilities */

#include "object.h"
#include <stdbool.h>

// NOLINTBEGIN(bugprone-macro-parentheses)
/** Loop over the KV pairs in a map. */
#define GGL_MAP_FOREACH(name, map) \
    for (GglKV *name = (map).pairs; name < &(map).pairs[(map).len]; \
         name = &name[1])
// NOLINTEND(bugprone-macro-parentheses)

/** Get the value corresponding with a key.
 * Returns whether the key was found in the map.
 * If `result` is not NULL it is set to the found value or NULL. */
bool ggl_map_get(GglMap map, GglBuffer key, GglObject **result);

#endif
