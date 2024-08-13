// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/map.h"
#include "ggl/buffer.h"
#include "ggl/object.h"
#include <stdbool.h>
#include <stdlib.h>

bool ggl_map_get(GglMap map, GglBuffer key, GglObject **result) {
    GGL_MAP_FOREACH(pair, map) {
        if (ggl_buffer_eq(key, pair->key)) {
            if (result != NULL) {
                *result = &pair->val;
            }
            return true;
        }
    }
    if (result != NULL) {
        *result = NULL;
    }
    return false;
}
