// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "bootstrap_manager.h"
#include "deployment_model.h"
#include "deployment_queue.h"
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <stdbool.h>
#include <stdint.h>

bool bootstrap_required(GglMap recipe, GglBuffer component_name) {
    GglObject *lifecycle_config;
    GglObject *bootstrap_config;

    GglError ret = ggl_map_validate(
        recipe,
        GGL_MAP_SCHEMA(
            { GGL_STR("Lifecycle"), false, GGL_TYPE_MAP, &lifecycle_config }
        )
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "Bootstrap is not required for %.*s. Received invalid recipe map.",
            (int) component_name.len,
            component_name.data
        );
        return false;
    }

    if (ggl_map_get(
            lifecycle_config->map, GGL_STR("bootstrap"), &bootstrap_config
        )) {
        GGL_LOGD(
            "Bootstrap is required for %.*s. Found bootstrap step in "
            "component recipe.",
            (int) component_name.len,
            component_name.data
        );

        // TODO: read and save bootstrap script to be processed
        return true;
    }
    GGL_LOGD(
        "Bootstrap is not required for %.*s. Did not find bootstrap "
        "step "
        "in component recipe.",
        (int) component_name.len,
        component_name.data
    );
    return false;
}

GglError save_deployment_state(
    GglDeployment *deployment, GglMap completed_components
) {
    /*
      deployment info will be saved to config in the following format:

        services:
          DeploymentService:
            deploymentState:
              components:
                component1:
                  lifecycle: component lifecycle state
                  version: component version
                component2:
                  lifecycle:
                  version:
                ...
              deploymentType: local/IoT Jobs
              deploymentDoc:
    */

    GglObject deployment_doc = GGL_OBJ_MAP(GGL_MAP(
        { GGL_STR("deployment_id"), GGL_OBJ_BUF(deployment->deployment_id) },
        { GGL_STR("recipe_directory_path"),
          GGL_OBJ_BUF(deployment->recipe_directory_path) },
        { GGL_STR("artifacts_directory_path"),
          GGL_OBJ_BUF(deployment->artifacts_directory_path) },
        { GGL_STR("configuration_arn"),
          GGL_OBJ_BUF(deployment->configuration_arn) },
        { GGL_STR("thing_group"), GGL_OBJ_BUF(deployment->thing_group) },
        { GGL_STR("components"), GGL_OBJ_MAP(deployment->components) }
    ));

    GglError ret = ggl_gg_config_write(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("DeploymentService"),
            GGL_STR("deploymentState"),
            GGL_STR("deploymentDoc")
        ),
        deployment_doc,
        &(int64_t) { 0 }
    );

    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to write deployment document to config.");
        return ret;
    }

    uint8_t deployment_type_mem[24] = { 0 };
    GglBuffer deployment_type = GGL_BUF(deployment_type_mem);
    if (deployment->type == LOCAL_DEPLOYMENT) {
        deployment_type = GGL_STR("LOCAL_DEPLOYMENT");
    } else if (deployment->type == THING_GROUP_DEPLOYMENT) {
        deployment_type = GGL_STR("THING_GROUP_DEPLOYMENT");
    }

    ret = ggl_gg_config_write(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("DeploymentService"),
            GGL_STR("deploymentState"),
            GGL_STR("deploymentType")
        ),
        GGL_OBJ_BUF(deployment_type),
        &(int64_t) { 0 }
    );

    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to write deployment type to config.");
        return ret;
    }

    /*
      component map format:

        component name:
          lifecycle_state:
          version:
    */
    GGL_MAP_FOREACH(component, completed_components) {
        GglBuffer component_name = component->key;

        if (component->val.type != GGL_TYPE_MAP) {
            GGL_LOGE("Component info object not of type map.");
            return GGL_ERR_INVALID;
        }

        GglObject component_info = GGL_OBJ_MAP(
            GGL_MAP({ component_name, GGL_OBJ_MAP(component->val.map) })
        );

        ret = ggl_gg_config_write(
            GGL_BUF_LIST(
                GGL_STR("services"),
                GGL_STR("DeploymentService"),
                GGL_STR("deploymentState"),
                GGL_STR("components")
            ),
            component_info,
            &(int64_t) { 0 }
        );

        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "Failed to write component info for %.*s to config.",
                (int) component_name.len,
                component_name.data
            );
            return ret;
        }
    }

    return GGL_ERR_OK;
}

GglError retrieve_in_progress_deployment(
    GglDeployment *deployment, GglKVVec *deployed_components
) {
    GglBuffer config_mem = GGL_BUF((uint8_t[2500]) { 0 });
    GglBumpAlloc balloc = ggl_bump_alloc_init(config_mem);
    GglObject deployment_config;

    GglError ret = ggl_gg_config_read(
        GGL_BUF_LIST(
            GGL_STR("services"),
            GGL_STR("DeploymentService"),
            GGL_STR("deploymentState")
        ),
        &balloc.alloc,
        &deployment_config
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    if (deployment_config.type != GGL_TYPE_MAP) {
        GGL_LOGE("Retrieved config not a map.");
        return GGL_ERR_INVALID;
    }

    GglObject *components_config;
    ret = ggl_map_validate(
        deployment_config.map,
        GGL_MAP_SCHEMA(
            { GGL_STR("components"), false, GGL_TYPE_MAP, &components_config }
        )
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GGL_MAP_FOREACH(component, components_config->map) {
        if (component->val.type != GGL_TYPE_MAP) {
            GGL_LOGE("Component info retrieved from config not of type map.");
            return GGL_ERR_INVALID;
        }

        // TODO: does this need to be memcopied into the vector?
        ret = ggl_kv_vec_push(
            deployed_components, (GglKV) { component->key, component->val }
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to add deployed component to vector.");
            return ret;
        }
    }

    GglObject *deployment_type;
    ret = ggl_map_validate(
        deployment_config.map,
        GGL_MAP_SCHEMA(
            { GGL_STR("deploymentType"), false, GGL_TYPE_BUF, &deployment_type }
        )
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (ggl_buffer_eq(deployment_type->buf, GGL_STR("LOCAL_DEPLOYMENT"))) {
        deployment->type = LOCAL_DEPLOYMENT;
    } else if (ggl_buffer_eq(
                   deployment_type->buf, GGL_STR("THING_GROUP_DEPLOYMENT")
               )) {
        deployment->type = THING_GROUP_DEPLOYMENT;
    }

    GglObject *deployment_doc;
    ret = ggl_map_validate(
        deployment_config.map,
        GGL_MAP_SCHEMA(
            { GGL_STR("deploymentDoc"), false, GGL_TYPE_MAP, &deployment_doc }
        )
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GglObject *deployment_id;
    ret = ggl_map_validate(
        deployment_doc->map,
        GGL_MAP_SCHEMA(
            { GGL_STR("deployment_id"), true, GGL_TYPE_BUF, &deployment_id }
        )
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    deployment->deployment_id = deployment_id->buf;

    GglObject *recipe_directory_path;
    ret = ggl_map_validate(
        deployment_doc->map,
        GGL_MAP_SCHEMA({ GGL_STR("recipe_directory_path"),
                         true,
                         GGL_TYPE_BUF,
                         &recipe_directory_path })
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    deployment->recipe_directory_path = recipe_directory_path->buf;

    GglObject *artifacts_directory_path;
    ret = ggl_map_validate(
        deployment_doc->map,
        GGL_MAP_SCHEMA({ GGL_STR("artifacts_directory_path"),
                         true,
                         GGL_TYPE_BUF,
                         &artifacts_directory_path })
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    deployment->artifacts_directory_path = artifacts_directory_path->buf;

    GglObject *configuration_arn;
    ret = ggl_map_validate(
        deployment_doc->map,
        GGL_MAP_SCHEMA({ GGL_STR("configuration_arn"),
                         true,
                         GGL_TYPE_BUF,
                         &configuration_arn })
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    deployment->configuration_arn = configuration_arn->buf;

    GglObject *thing_group;
    ret = ggl_map_validate(
        deployment_doc->map,
        GGL_MAP_SCHEMA(
            { GGL_STR("thing_group"), true, GGL_TYPE_BUF, &thing_group }
        )
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    deployment->thing_group = thing_group->buf;

    GglObject *components;
    ret = ggl_map_validate(
        deployment_doc->map,
        GGL_MAP_SCHEMA(
            { GGL_STR("components"), true, GGL_TYPE_MAP, &components }
        )
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    deployment->components = components->map;

    static uint8_t deployment_deep_copy_mem[5000] = { 0 };
    GglBumpAlloc deployment_balloc
        = ggl_bump_alloc_init(GGL_BUF(deployment_deep_copy_mem));
    ret = deep_copy_deployment(deployment, &deployment_balloc.alloc);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to deep copy deployment.");
        return ret;
    }

    // delete config entries once retrieved to avoid future conflicts
    ret = delete_saved_deployment_from_config();
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return GGL_ERR_OK;
}

GglError delete_saved_deployment_from_config(void) {
    GglError ret = ggl_gg_config_delete(GGL_BUF_LIST(
        GGL_STR("services"),
        GGL_STR("DeploymentService"),
        GGL_STR("deploymentState")
    ));

    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "Failed to delete previously saved deployment state from config."
        );
        return ret;
    }

    return GGL_ERR_OK;
}
