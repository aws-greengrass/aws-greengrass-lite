// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "make_config_path_object.h"
#include <assert.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <stddef.h>

GglObject *ggl_make_config_path_object(
    GglObject *component_name_object, GglObject *key_path_object
) {
    assert(key_path_object->list.len + 2 < MAXIMUM_KEY_PATH_DEPTH);
    static GglObject objects[MAXIMUM_KEY_PATH_DEPTH];
    GglObjVec path = GGL_OBJ_VEC(objects);
    ggl_obj_vec_push(&path, GGL_OBJ_STR("services"));
    ggl_obj_vec_push(&path, *component_name_object);
    for (size_t index = 0; index < key_path_object->list.len; index++) {
        ggl_obj_vec_push(&path, key_path_object->list.items[index]);
    }
    static GglObject path_object;
    path_object = GGL_OBJ(path.list);
    return &path_object;
}
