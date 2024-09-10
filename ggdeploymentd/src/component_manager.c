// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "component_manager.h"
#include "component_model.h"
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>

#define LOCAL_DEPLOYMENT "LOCAL_DEPLOYMENT"

static void find_active_version(
    GglBuffer package_name, ComponentIdentifier *component
) {
    GglMap params = GGL_MAP(
        { GGL_STR("key_path"),
          GGL_OBJ_LIST(GGL_OBJ_STR("services"), GGL_OBJ(package_name)) }
    );

    static uint8_t resp_mem[128] = { 0 };
    GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(resp_mem));

    // check the config to see if the provided package name is already a running
    // service
    GglObject resp;
    GglError ret = ggl_call(
        GGL_STR("/aws/ggl/ggconfigd"),
        GGL_STR("read"),
        params,
        NULL,
        &balloc.alloc,
        &resp
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW(
            "component-manager",
            "No active running version of %s found.",
            package_name.data
        );
        return;
    }
    if (resp.type != GGL_TYPE_BUF) {
        GGL_LOGW(
            "component-manager",
            "Configuration package name is not a string. Assuming no active "
            "version of %s found.",
            package_name.data
        );
        return;
    }

    // find the version of the active running component
    GglObject version_resp;
    GglMap version_params = GGL_MAP(
        { GGL_STR("key_path"),
          GGL_OBJ_LIST(GGL_OBJ_STR("services"), resp, GGL_OBJ_STR("version")) }
    );
    static uint8_t version_resp_mem[128] = { 0 };
    GglBumpAlloc version_balloc
        = ggl_bump_alloc_init(GGL_BUF(version_resp_mem));

    GglError version_ret = ggl_call(
        GGL_STR("/aws/ggl/ggconfigd"),
        GGL_STR("read"),
        version_params,
        NULL,
        &version_balloc.alloc,
        &version_resp
    );
    if (version_ret != GGL_ERR_OK) {
        // TODO: should we error out here? component is in service config but
        // does not have a version key listed. realistically this should not
        // happen
        GGL_LOGW(
            "component-manager",
            "Unable to retrieve version of %s. Assuming no active version "
            "found.",
            package_name.data
        );
        return;
    }
    if (version_resp.type != GGL_TYPE_BUF) {
        // same as above
        GGL_LOGW(
            "component-manager",
            "Configuration version is not a string. Assuming no active version "
            "of %s found.",
            package_name.data
        );
        return;
    }

    // active component found, update the ComponentIdentifier
    component->name = resp.buf;
    component->version = version_resp.buf;
}

static GglError merge_version_requirements(
    GglMap version_requirements, GglBuffer *requirement
) {
    // TODO: figure out how to merge when there's multiple groups and
    // requirements listed in the map. return a string of the final requirement
    return GGL_ERR_OK;
}

static GglError find_best_candidate_locally(
    ComponentIdentifier *identifier,
    GglBuffer component_name,
    GglMap version_requirements
) {
    GGL_LOGD(
        "component-manager",
        "Searching for the best local candidate on the device."
    );

    GglBuffer *merged_requirements = NULL;
    merge_version_requirements(version_requirements, merged_requirements);

    if (merged_requirements == NULL) {
        GGL_LOGE("component-manager", "Failed to merge version requiremtns.");
        return GGL_ERR_FAILURE;
    }

    ComponentIdentifier *active_component = NULL;
    find_active_version(component_name, active_component);

    if (active_component != NULL) {
        GGL_LOGI(
            "component-manager",
            "Found running component which meets the version requirements."
        );
        identifier = active_component;
    } else {
        GGL_LOGI(
            "component-manager",
            "No running component satisfies the verison requirements. "
            "Searching in the local component store."
        );
        // TODO: add component store logic
    }

    return GGL_ERR_OK;
}

static GglError negotiate_version_with_cloud(
    GglBuffer component_name,
    GglMap version_requirements,
    ComponentIdentifier *local_candidate
) {
    if (local_candidate == NULL) {
        // we use some aws sdk stuff here, check how to do that in c
    }
    return GGL_ERR_OK;
}

ComponentMetadata resolve_component_version(
    GglBuffer component_name, GglMap version_requirements
) {
    // NOTE: verison_requirements is a map of groups to the version requirements
    // of the group ex: LOCAL_DEPLOYMENT -> >=1.0.0 <2.0.0
    //               group1 -> ==1.0.0
    GGL_LOGD("component-manager", "Resolving component version.");

    // find best local candidate
    ComponentIdentifier *local_candidate = NULL;
    find_best_candidate_locally(
        local_candidate, component_name, version_requirements
    );

    bool local_candidate_found;
    if (local_candidate != NULL) {
        GGL_LOGI(
            "component-manager",
            "Found the best local candidate that satisfies the requirement."
        );
        local_candidate_found = true;
    } else {
        GGL_LOGI(
            "component-manager",
            "Failed to find a local candidate that satisfies the requrement."
        );
        local_candidate_found = false;
    }

    ComponentIdentifier *resolved_component;
    GglObject *val;
    // TODO: also check that the component region matches the expected region
    // (component store functionality)
    if (ggl_map_get(version_requirements, GGL_STR(LOCAL_DEPLOYMENT), &val)
        && local_candidate_found) {
        GGL_LOGI(
            "component-manager",
            "Local group has a requirement and found satisfying local "
            "candidate. Using the local candidate as the resolved version "
            "without negotiating with the cloud."
        );
        resolved_component = local_candidate;
    } else {
        // TODO: if we find a local version, skip negotiating with the cloud
        // if there is no local version and cloud negotiation fails, fail the
        // deployment

        // negotiate with cloud
        negotiate_version_with_cloud(
            component_name, version_requirements, local_candidate
        );
    }
}
