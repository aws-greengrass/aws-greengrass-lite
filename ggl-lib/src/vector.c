// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/vector.h"
#include "ggl/error.h"
#include "ggl/log.h"
#include "ggl/object.h"
#include <stdlib.h>

GglError ggl_obj_vec_push(GglObjVec *vector, GglObject object) {
    if (vector->list.len >= vector->capacity) {
        return GGL_ERR_NOMEM;
    }
    GGL_LOGT("ggl_obj_vec", "Pushed to %p.", vector);
    vector->list.items[vector->list.len] = object;
    vector->list.len++;
    return GGL_ERR_OK;
}

GglError ggl_obj_vec_pop(GglObjVec *vector, GglObject *out) {
    if (vector->list.len == 0) {
        return GGL_ERR_RANGE;
    }
    if (out != NULL) {
        *out = vector->list.items[vector->list.len - 1];
    }
    GGL_LOGT("ggl_obj_vec", "Popped from %p.", vector);

    vector->list.len--;
    return GGL_ERR_OK;
}
