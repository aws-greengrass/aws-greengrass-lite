// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#ifndef RECIPE_FILE_OPERATION_H
#define RECIPE_FILE_OPERATION_H

#include <ggl/alloc.h>
#include <ggl/error.h>
#include <ggl/object.h>

GglError deserialize_file_content(
    char *file_path,
    GglBuffer recipe_str_buf,
    GglAlloc *alloc,
    GglObject *recipe_obj
);

GglError open_file(char *file_path, GglBuffer *recipe_obj);

#endif
