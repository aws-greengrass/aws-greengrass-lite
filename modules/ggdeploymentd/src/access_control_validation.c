// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "access_control_validation.h"
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/list.h>
#include <gg/map.h>
#include <gg/object.h>
#include <ggl/policy_validation.h>
#include <stddef.h>

GgError validate_access_control_policies(GgObject *config_obj) {
    if (config_obj == NULL) {
        return GG_ERR_OK;
    }
    if (gg_obj_type(*config_obj) != GG_TYPE_MAP) {
        return GG_ERR_OK;
    }

    GgObject *ac_obj;
    if (!gg_map_get(
            gg_obj_into_map(*config_obj), GG_STR("accessControl"), &ac_obj
        )) {
        return GG_ERR_OK;
    }
    if (gg_obj_type(*ac_obj) != GG_TYPE_MAP) {
        return GG_ERR_OK;
    }

    GG_MAP_FOREACH (service_kv, gg_obj_into_map(*ac_obj)) {
        if (gg_obj_type(*gg_kv_val(service_kv)) != GG_TYPE_MAP) {
            continue;
        }
        GG_MAP_FOREACH (policy_kv, gg_obj_into_map(*gg_kv_val(service_kv))) {
            if (gg_obj_type(*gg_kv_val(policy_kv)) != GG_TYPE_MAP) {
                continue;
            }
            GgObject *resources_obj;
            if (!gg_map_get(
                    gg_obj_into_map(*gg_kv_val(policy_kv)),
                    GG_STR("resources"),
                    &resources_obj
                )) {
                continue;
            }
            if (gg_obj_type(*resources_obj) != GG_TYPE_LIST) {
                continue;
            }
            GG_LIST_FOREACH (res_obj, gg_obj_into_list(*resources_obj)) {
                if (gg_obj_type(*res_obj) != GG_TYPE_BUF) {
                    continue;
                }
                GgBuffer res = gg_obj_into_buf(*res_obj);
                GgError ret = ggl_validate_policy_resource(res);
                if (ret != GG_ERR_OK) {
                    return ret;
                }
            }
        }
    }
    return GG_ERR_OK;
}

GgError validate_merge_access_control(
    GglDeployment *deployment, GgBuffer component_name
) {
    GgObject *doc_component_info;
    if (!gg_map_get(
            deployment->components, component_name, &doc_component_info
        )) {
        return GG_ERR_OK;
    }
    if (gg_obj_type(*doc_component_info) != GG_TYPE_MAP) {
        return GG_ERR_OK;
    }

    GgObject *config_update;
    if (!gg_map_get(
            gg_obj_into_map(*doc_component_info),
            GG_STR("configurationUpdate"),
            &config_update
        )) {
        return GG_ERR_OK;
    }
    if (gg_obj_type(*config_update) != GG_TYPE_MAP) {
        return GG_ERR_OK;
    }

    GgObject *merge_obj;
    if (!gg_map_get(
            gg_obj_into_map(*config_update), GG_STR("merge"), &merge_obj
        )) {
        return GG_ERR_OK;
    }

    return validate_access_control_policies(merge_obj);
}
