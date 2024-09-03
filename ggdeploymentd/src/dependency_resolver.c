// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#define _GNU_SOURCE

#include "dependency_resolver.h"
#include "component_model.h"
#include "deployment_model.h"
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/vector.h>

// static resolve_component_dependencies(GglBuffer target_component_name, GglMap
// component_name_to_version_constraints, GglMap resolved_components, GglMap
// component_incoming_reference_count, )

static ComponentMetadata resolve_component_version(
    GglBuffer component_name, GglMap version_requirements
) {
}

GglList resolve_dependencies(GglDeployment *deployment) {
    GGL_LOGD("dependency-resolver", "Starting dependency resolution.");
    // {component_name -> {dependent_component_name -> version_constraint}}
    GglMap component_name_to_version_constraints;

    //
    GglList target_components_to_resolve;

    // GglKVVec kv_buffer_vec = { .map = (GglMap) { .pairs = kv_buffer, .len = 0
    // },
    //                            .capacity = children_count };

    if (deployment->root_component_versions_to_add.len != 0) {
        GGL_MAP_FOREACH(pair, deployment->root_component_versions_to_add) {
            // going through each component that we will be adding. we have the
            // version of the component, how do i get its requirements???
            if (pair->val.type != GGL_TYPE_BUF) {
                GGL_LOGE(
                    "dependency-resolver",
                    "Component version not of type buffer."
                );
                GglList ret = { .len = 0 };
                return ret;
            }

            ComponentIdentifier identifier
                = { .name = pair->key, .version = pair->val.buf };
        }
    }
}
