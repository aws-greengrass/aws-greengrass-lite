// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "config_path_object.h"
#include <gg/buffer.h>
#include <gg/log.h>
#include <gg/object.h>
#include <gg/vector.h>
#include <stddef.h>

/// The max component config path depth
// Takes into account `services.myComponent.configuration` at the beginning of
// e.g. `myComponent`'s config path in the database
#define GGL_MAX_COMPONENT_CONFIG_DEPTH (GG_MAX_OBJECT_DEPTH - 3)

GgError ggl_make_config_path_object(
    GgBuffer component_name, GgList key_path, GgBufList *result
) {
    static GgBuffer full_key_path_mem[GG_MAX_OBJECT_DEPTH];
    GgBufVec full_key_path = GG_BUF_VEC(full_key_path_mem);

    GgError ret = gg_buf_vec_push(&full_key_path, GG_STR("services"));
    gg_buf_vec_chain_push(&ret, &full_key_path, component_name);
    gg_buf_vec_chain_push(&ret, &full_key_path, GG_STR("configuration"));
    gg_buf_vec_chain_append_list(&ret, &full_key_path, key_path);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Key path too long.");
        return ret;
    }

    *result = full_key_path.buf_list;
    return GG_ERR_OK;
}

GgError ggl_parse_config_path(
    GgList config_path, GgBuffer *component_name, GgList *key_path
) {
    if (config_path.len < 4) {
        GG_LOGE("Config path is not in the expected format");
        return GG_ERR_INVALID;
    }

    *component_name = gg_obj_into_buf(config_path.items[1]);

    static GgObject component_key_path_mem[GGL_MAX_COMPONENT_CONFIG_DEPTH];
    static GgObjVec component_key_path = GG_OBJ_VEC(component_key_path_mem);
    component_key_path.list.len = 0;

    GgError ret = gg_obj_vec_push(&component_key_path, config_path.items[3]);
    for (size_t i = 4; i < config_path.len; i++) {
        gg_obj_vec_chain_push(&ret, &component_key_path, config_path.items[i]);
    }
    if (ret != GG_ERR_OK) {
        GG_LOGE("Key path too long.");
        return ret;
    }

    *key_path = component_key_path.list;
    return GG_ERR_OK;
}
