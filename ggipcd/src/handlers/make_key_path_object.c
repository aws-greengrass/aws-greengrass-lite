// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "make_key_path_object.h"
#include <assert.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <stddef.h>

GglObject *make_key_path_object(
    GglObject *component_name_object, GglObject *key_path_object
) {
    assert(key_path_object->list.len + 2 < MAXIMUM_KEY_PATH_DEPTH);
    static GglObject objects[MAXIMUM_KEY_PATH_DEPTH];
    static GglList path_list = { .items = objects, .len = 0 };
    GglObjVec path = { .list = path_list, .capacity = MAXIMUM_KEY_PATH_DEPTH };
    ggl_obj_vec_push(&path, GGL_OBJ_STR("services"));
    ggl_obj_vec_push(&path, *component_name_object);
    for (size_t index = 0; index < key_path_object->list.len; index++) {
        ggl_obj_vec_push(&path, key_path_object->list.items[index]);
    }
    static GglObject path_object;
    path_object.type = GGL_TYPE_LIST;
    path_object.list = path_list;
    return &path_object;
}
