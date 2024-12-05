// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "component_config.h"
#include "deployment_model.h"
#include <assert.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/list.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <stdbool.h>
#include <stddef.h>

static GglError apply_reset_config(
    GglBuffer component_name, GglMap component_config_map
) {
    GglObject *reset_configuration = NULL;
    GglError ret = ggl_map_validate(
        component_config_map,
        GGL_MAP_SCHEMA(
            { GGL_STR("reset"), false, GGL_TYPE_LIST, &reset_configuration }
        )
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    // If there is no reset configuration, then there is no
    // configuration update to make
    if (reset_configuration == NULL) {
        return GGL_ERR_OK;
    }

    if (reset_configuration->type != GGL_TYPE_LIST) {
        GGL_LOGE("Reset update did not parse into a list "
                 "during configuration updates.");
        return GGL_ERR_INVALID;
    }
    GGL_LIST_FOREACH(reset_element, reset_configuration->list) {
        if (reset_element->type != GGL_TYPE_BUF) {
            GGL_LOGE("Configuration key for reset config "
                     "update not provided as a buffer.");
            return GGL_ERR_INVALID;
        }
        ret = ggl_gg_config_delete(GGL_BUF_LIST(
            GGL_STR("services"),
            component_name,
            GGL_STR("configuration"),
            reset_element->buf
        ));

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
        GGL_MAP_SCHEMA(
            { GGL_STR("merge"), false, GGL_TYPE_MAP, &merge_configuration }
        )
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    // If there is no merge configuration, then there is no
    // configuration update to make
    if (merge_configuration == NULL) {
        return GGL_ERR_OK;
    }
    if (merge_configuration->type != GGL_TYPE_MAP) {
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
            { component_name, false, GGL_TYPE_MAP, &doc_component_info }
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
    if (doc_component_info->type != GGL_TYPE_MAP) {
        GGL_LOGE("Component information did not parse into a map during "
                 "configuration updates.");
        return GGL_ERR_INVALID;
    }

    GglObject *component_configuration = NULL;
    ret = ggl_map_validate(
        doc_component_info->map,
        GGL_MAP_SCHEMA({ GGL_STR("configurationUpdate"),
                         false,
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

    if (component_configuration->type != GGL_TYPE_MAP) {
        GGL_LOGE("Configuration update did not parse into a map during "
                 "configuration updates.");
        return GGL_ERR_INVALID;
    }

    if (ggl_buffer_eq(operation, GGL_STR("merge"))) {
        ret = apply_merge_config(component_name, component_configuration->map);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }
    if (ggl_buffer_eq(operation, GGL_STR("reset"))) {
        ret = apply_reset_config(component_name, component_configuration->map);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    return GGL_ERR_OK;
}
