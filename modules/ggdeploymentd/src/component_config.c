// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "component_config.h"
#include "deployment_model.h"
#include <assert.h>
#include <ggl/buffer.h>
#include <ggl/constants.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/flags.h>
#include <ggl/json_pointer.h>
#include <ggl/list.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <stddef.h>

static GglError apply_reset_config(
    GglBuffer component_name, GglMap component_config_map
) {
    GglObject *reset_configuration = NULL;
    GglError ret = ggl_map_validate(
        component_config_map,
        GGL_MAP_SCHEMA({ GGL_STR("reset"),
                         GGL_OPTIONAL,
                         GGL_TYPE_LIST,
                         &reset_configuration })
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    // If there is no reset configuration, then there is no
    // configuration update to make
    if (reset_configuration == NULL) {
        return GGL_ERR_OK;
    }

    if (ggl_obj_type(*reset_configuration) != GGL_TYPE_LIST) {
        GGL_LOGE("Reset update did not parse into a list "
                 "during configuration updates.");
        return GGL_ERR_INVALID;
    }
    GGL_LIST_FOREACH(reset_element, ggl_obj_into_list(*reset_configuration)) {
        if (ggl_obj_type(*reset_element) != GGL_TYPE_BUF) {
            GGL_LOGE("Configuration key for reset config "
                     "update not provided as a buffer.");
            return GGL_ERR_INVALID;
        }

        // Empty string means they want to reset the whole configuration to
        // default configuration.
        if (ggl_buffer_eq(ggl_obj_into_buf(*reset_element), GGL_STR(""))) {
            GGL_LOGI(
                "Received a request to reset the entire configuration for %.*s",
                (int) component_name.len,
                component_name.data
            );
            ret = ggl_gg_config_delete(GGL_BUF_LIST(
                GGL_STR("services"), component_name, GGL_STR("configuration")
            ));
            if (ret != GGL_ERR_OK) {
                GGL_LOGE(
                    "Error while deleting the component %.*s's configuration.",
                    (int) component_name.len,
                    component_name.data
                );
                return ret;
            }

            break;
        }

        static GglBuffer key_path_mem[GGL_MAX_OBJECT_DEPTH];
        GglBufVec key_path = GGL_BUF_VEC(key_path_mem);
        ret = ggl_buf_vec_push(&key_path, GGL_STR("services"));
        ggl_buf_vec_chain_push(&ret, &key_path, component_name);
        ggl_buf_vec_chain_push(&ret, &key_path, GGL_STR("configuration"));
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Too many configuration levels during config reset.");
            return ret;
        }

        ret = ggl_gg_config_jsonp_parse(
            ggl_obj_into_buf(*reset_element), &key_path
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Error parsing json pointer for config reset");
            return ret;
        }

        ret = ggl_gg_config_delete(key_path.buf_list);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "Failed to perform configuration reset updates "
                "for component %.*s.",
                (int) component_name.len,
                component_name.data
            );
            return ret;
        }

        GGL_LOGI(
            "Made a configuration reset update for component "
            "%.*s",
            (int) component_name.len,
            component_name.data
        );
    }

    return GGL_ERR_OK;
}

static GglError apply_merge_config(
    GglBuffer component_name, GglMap component_config_map
) {
    GglObject *merge_configuration = NULL;
    GglError ret = ggl_map_validate(
        component_config_map,
        GGL_MAP_SCHEMA({ GGL_STR("merge"),
                         GGL_OPTIONAL,
                         GGL_TYPE_MAP,
                         &merge_configuration })
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    // If there is no merge configuration, then there is no
    // configuration update to make
    if (merge_configuration == NULL) {
        return GGL_ERR_OK;
    }
    if (ggl_obj_type(*merge_configuration) != GGL_TYPE_MAP) {
        GGL_LOGE("Merge update did not parse into a map during "
                 "configuration updates.");
        return GGL_ERR_INVALID;
    }

    // TODO: Use deployment timestamp not the current timestamp
    // after we support deployment timestamp
    ret = ggl_gg_config_write(
        GGL_BUF_LIST(
            GGL_STR("services"), component_name, GGL_STR("configuration")
        ),
        *merge_configuration,
        NULL
    );

    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "Failed to write configuration merge updates for "
            "component %.*s to ggconfigd.",
            (int) component_name.len,
            component_name.data
        );
        return ret;
    }

    GGL_LOGI(
        "Made a configuration merge update for component %.*s",
        (int) component_name.len,
        component_name.data
    );

    return GGL_ERR_OK;
}

GglError apply_configurations(
    GglDeployment *deployment, GglBuffer component_name, GglBuffer operation
) {
    assert(
        ggl_buffer_eq(operation, GGL_STR("merge"))
        || ggl_buffer_eq(operation, GGL_STR("reset"))
    );

    GglObject *doc_component_info = NULL;
    GglError ret = ggl_map_validate(
        deployment->components,
        GGL_MAP_SCHEMA(
            { component_name, GGL_OPTIONAL, GGL_TYPE_MAP, &doc_component_info }
        )
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    // No config items to write if the component is not a root component in
    // the deployment
    if (doc_component_info == NULL) {
        return GGL_ERR_OK;
    }
    if (ggl_obj_type(*doc_component_info) != GGL_TYPE_MAP) {
        GGL_LOGE("Component information did not parse into a map during "
                 "configuration updates.");
        return GGL_ERR_INVALID;
    }

    GglObject *component_configuration = NULL;
    ret = ggl_map_validate(
        ggl_obj_into_map(*doc_component_info),
        GGL_MAP_SCHEMA({ GGL_STR("configurationUpdate"),
                         GGL_OPTIONAL,
                         GGL_TYPE_MAP,
                         &component_configuration })
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    // No config items to write if there is no configurationUpdate item
    if (component_configuration == NULL) {
        return GGL_ERR_OK;
    }

    if (ggl_obj_type(*component_configuration) != GGL_TYPE_MAP) {
        GGL_LOGE("Configuration update did not parse into a map during "
                 "configuration updates.");
        return GGL_ERR_INVALID;
    }

    if (ggl_buffer_eq(operation, GGL_STR("merge"))) {
        ret = apply_merge_config(
            component_name, ggl_obj_into_map(*component_configuration)
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }
    if (ggl_buffer_eq(operation, GGL_STR("reset"))) {
        ret = apply_reset_config(
            component_name, ggl_obj_into_map(*component_configuration)
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    return GGL_ERR_OK;
}
