// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#define _GNU_SOURCE

#include "component_manager.h"
#include "component_model.h"
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>

#define LOCAL_DEPLOYMENT "LOCAL_DEPLOYMENT"

static GglError find_active_version(
    GglBuffer package_name, ComponentIdentifier *component
) {
    // TODO: how can we find active components running on the device? need to
    // return the active version of the provided package currently running if it
    // exists

    // update: we can check the config for the service name of the component
    // can also poll the config for the component's version
    // if it's there we're happy if not then there isn't an active version running

    GglMap params = GGL_MAP(
        { GGL_STR("component_name"), GGL_OBJ(package_name) }
    );

    static uint8_t resp_mem[128] = { 0 };
    GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(resp_mem));

    GglObject resp;
    GglError ret = ggl_call(
        GGL_STR("/aws/ggl/gghealthd"),
        GGL_STR("get_status"),
        params,
        NULL,
        &balloc.alloc,
        &resp
    );
    if(ret == GGL_ERR_OK) {
        if(resp.type != GGL_TYPE_MAP) {
            GGL_LOGD("component-manager", "Received invalid response while searching for active versions of %s. No running component satisfies the requirement.", package_name.data);
            return ret;
        }
        GglObject *val;
        if(ggl_map_get(resp.map, GGL_STR("lifecycle_state"), &val)) {
            if(val->type != GGL_TYPE_BUF) {
                GGL_LOGE("component-manager", "Received invalid lifecycle state response, expected buffer.");
                return GGL_ERR_INVALID;
            }
            if(val->buf.data == "NEW") {
                
            }
        } else {
            GGL_LOGD("component-manager", "No running component satisfies the requirement.");
        }

    } else {
        GGL_LOGE("component-manager", "Encountered error while searching for active versions of %s. No running component satisfies the requirement.", package_name.data);
        return ret;
    }
    return GGL_ERR_OK;
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
        // negotiate with cloud

        // TODO: need to check if the device is properly configured to talk to
        // the cloud
        //       follow up to see if we have something similar to
        //       deviceConfiguration.isDeviceConfiguredToTalkToCloud in java
        negotiate_version_with_cloud(component_name, version_requirements, local_candidate);

        // if the device is not able to talk to the cloud we would use the local
        // candidate if present otherwise fail
    }
}
