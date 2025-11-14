// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "component_config.h"
#include "deployment_model.h"
#include <assert.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/flags.h>
#include <gg/list.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/vector.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/json_pointer.h>
#include <stddef.h>

static GgError apply_reset_config(
    GgBuffer component_name, GgMap component_config_map
) {
    GgObject *reset_configuration = NULL;
    GgError ret = gg_map_validate(
        component_config_map,
        GG_MAP_SCHEMA(
            { GG_STR("reset"), GG_OPTIONAL, GG_TYPE_LIST, &reset_configuration }
        )
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    // If there is no reset configuration, then there is no
    // configuration update to make
    if (reset_configuration == NULL) {
        return GG_ERR_OK;
    }

    if (gg_obj_type(*reset_configuration) != GG_TYPE_LIST) {
        GG_LOGE(
            "Reset update did not parse into a list during configuration updates."
        );
        return GG_ERR_INVALID;
    }
    GG_LIST_FOREACH (reset_element, gg_obj_into_list(*reset_configuration)) {
        if (gg_obj_type(*reset_element) != GG_TYPE_BUF) {
            GG_LOGE(
                "Configuration key for reset config update not provided as a buffer."
            );
            return GG_ERR_INVALID;
        }

        // Empty string means they want to reset the whole configuration to
        // default configuration.
        if (gg_buffer_eq(gg_obj_into_buf(*reset_element), GG_STR(""))) {
            GG_LOGI(
                "Received a request to reset the entire configuration for %.*s",
                (int) component_name.len,
                component_name.data
            );
            ret = ggl_gg_config_delete(GG_BUF_LIST(
                GG_STR("services"), component_name, GG_STR("configuration")
            ));
            if (ret != GG_ERR_OK) {
                GG_LOGE(
                    "Error while deleting the component %.*s's configuration.",
                    (int) component_name.len,
                    component_name.data
                );
                return ret;
            }

            break;
        }

        static GgBuffer key_path_mem[GG_MAX_OBJECT_DEPTH];
        GgBufVec key_path = GG_BUF_VEC(key_path_mem);
        ret = gg_buf_vec_push(&key_path, GG_STR("services"));
        gg_buf_vec_chain_push(&ret, &key_path, component_name);
        gg_buf_vec_chain_push(&ret, &key_path, GG_STR("configuration"));
        if (ret != GG_ERR_OK) {
            GG_LOGE("Too many configuration levels during config reset.");
            return ret;
        }

        ret = ggl_gg_config_jsonp_parse(
            gg_obj_into_buf(*reset_element), &key_path
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE("Error parsing json pointer for config reset");
            return ret;
        }

        ret = ggl_gg_config_delete(key_path.buf_list);
        if (ret != GG_ERR_OK) {
            GG_LOGE(
                "Failed to perform configuration reset updates for component %.*s.",
                (int) component_name.len,
                component_name.data
            );
            return ret;
        }

        GG_LOGI(
            "Made a configuration reset update for component %.*s",
            (int) component_name.len,
            component_name.data
        );
    }

    return GG_ERR_OK;
}

static GgError apply_merge_config(
    GgBuffer component_name, GgMap component_config_map
) {
    GgObject *merge_configuration = NULL;
    GgError ret = gg_map_validate(
        component_config_map,
        GG_MAP_SCHEMA(
            { GG_STR("merge"), GG_OPTIONAL, GG_TYPE_MAP, &merge_configuration }
        )
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    // If there is no merge configuration, then there is no
    // configuration update to make
    if (merge_configuration == NULL) {
        return GG_ERR_OK;
    }
    if (gg_obj_type(*merge_configuration) != GG_TYPE_MAP) {
        GG_LOGE(
            "Merge update did not parse into a map during configuration updates."
        );
        return GG_ERR_INVALID;
    }

    // TODO: Use deployment timestamp not the current timestamp
    // after we support deployment timestamp
    ret = ggl_gg_config_write(
        GG_BUF_LIST(
            GG_STR("services"), component_name, GG_STR("configuration")
        ),
        *merge_configuration,
        NULL
    );

    if (ret != GG_ERR_OK) {
        GG_LOGE(
            "Failed to write configuration merge updates for component %.*s to ggconfigd.",
            (int) component_name.len,
            component_name.data
        );
        return ret;
    }

    GG_LOGI(
        "Made a configuration merge update for component %.*s",
        (int) component_name.len,
        component_name.data
    );

    return GG_ERR_OK;
}

GgError apply_configurations(
    GglDeployment *deployment, GgBuffer component_name, GgBuffer operation
) {
    assert(
        gg_buffer_eq(operation, GG_STR("merge"))
        || gg_buffer_eq(operation, GG_STR("reset"))
    );

    GgObject *doc_component_info = NULL;
    GgError ret = gg_map_validate(
        deployment->components,
        GG_MAP_SCHEMA(
            { component_name, GG_OPTIONAL, GG_TYPE_MAP, &doc_component_info }
        )
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    // No config items to write if the component is not a root component in
    // the deployment
    if (doc_component_info == NULL) {
        return GG_ERR_OK;
    }
    if (gg_obj_type(*doc_component_info) != GG_TYPE_MAP) {
        GG_LOGE(
            "Component information did not parse into a map during configuration updates."
        );
        return GG_ERR_INVALID;
    }

    GgObject *component_configuration = NULL;
    ret = gg_map_validate(
        gg_obj_into_map(*doc_component_info),
        GG_MAP_SCHEMA({ GG_STR("configurationUpdate"),
                        GG_OPTIONAL,
                        GG_TYPE_MAP,
                        &component_configuration })
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    // No config items to write if there is no configurationUpdate item
    if (component_configuration == NULL) {
        return GG_ERR_OK;
    }

    if (gg_obj_type(*component_configuration) != GG_TYPE_MAP) {
        GG_LOGE(
            "Configuration update did not parse into a map during configuration updates."
        );
        return GG_ERR_INVALID;
    }

    if (gg_buffer_eq(operation, GG_STR("merge"))) {
        ret = apply_merge_config(
            component_name, gg_obj_into_map(*component_configuration)
        );
        if (ret != GG_ERR_OK) {
            return ret;
        }
    }
    if (gg_buffer_eq(operation, GG_STR("reset"))) {
        ret = apply_reset_config(
            component_name, gg_obj_into_map(*component_configuration)
        );
        if (ret != GG_ERR_OK) {
            return ret;
        }
    }

    return GG_ERR_OK;
}
