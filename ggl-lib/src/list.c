// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/list.h"
#include "ggl/error.h"
#include "ggl/log.h"
#include "ggl/object.h"

GglError ggl_list_type_check(GglList list, GglObjectType type) {
    GGL_LIST_FOREACH(elem, list) {
        if (elem->type != type) {
            GGL_LOGE("list", "List element is of invalid type.");
            return GGL_ERR_PARSE;
        }
    }
    return GGL_ERR_OK;
}
