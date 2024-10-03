// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "component_manager.h"
#include "component_store.h"
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LOCAL_DEPLOYMENT "LOCAL_DEPLOYMENT"

static GglError find_active_version(
    GglBuffer package_name, GglBuffer *version
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
        return GGL_ERR_NOENTRY;
    }
    if (resp.type != GGL_TYPE_BUF) {
        GGL_LOGW(
            "component-manager",
            "Configuration package name is not a string. Assuming no active "
            "version of %s found.",
            package_name.data
        );
        return GGL_ERR_NOENTRY;
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
        return GGL_ERR_NOENTRY;
    }
    if (version_resp.type != GGL_TYPE_BUF) {
        GGL_LOGW(
            "component-manager",
            "Configuration version is not a string. Assuming no active version "
            "of %s found.",
            package_name.data
        );
        return GGL_ERR_NOENTRY;
    }

    // active component found, update the version
    *version = version_resp.buf;
    return GGL_ERR_OK;
}

static GglError find_best_candidate_locally(
    GglBuffer component_name, GglMap version_requirements, GglBuffer *version
) {
    GGL_LOGD(
        "component-manager",
        "Searching for the best local candidate on the device."
    );

    GglError ret = find_active_version(component_name, version);

    if (ret == GGL_ERR_OK) {
        GGL_LOGI(
            "component-manager",
            "Found running component which meets the version requirements."
        );
    } else {
        GGL_LOGI(
            "component-manager",
            "No running component satisfies the version requirements. "
            "Searching in the local component store."
        );
        // TODO: double check that we should be checking the local deployment
        // group here
        GglObject *val;
        if (!ggl_map_get(
                version_requirements, GGL_STR(LOCAL_DEPLOYMENT), &val
            )) {
            GGL_LOGW(
                "component-manager",
                "Failed to find requirements for local deployment group."
            );
            // return ok since we will proceed with cloud negotiation
            return GGL_ERR_NOENTRY;
        }
        if (val->type != GGL_TYPE_BUF) {
            GGL_LOGW(
                "component-manager",
                "Local deployment requirements not of type buffer."
            );
            return GGL_ERR_NOENTRY;
        }

        find_available_component(component_name, val->buf, version);
    }

    return GGL_ERR_OK;
}

bool resolve_component_version(
    GglBuffer component_name, GglMap version_requirements, GglBuffer *version
) {
    // NOTE: version_requirements is a map of groups to the version requirements
    // of the group ex: LOCAL_DEPLOYMENT -> >=1.0.0 <2.0.0
    //               group1 -> ==1.0.0
    GGL_LOGD("component-manager", "Resolving component version.");

    // find best local candidate
    GglBuffer local_version;
    GglError ret = find_best_candidate_locally(
        component_name, version_requirements, &local_version
    );

    bool local_candidate_found;
    if (ret == GGL_ERR_OK) {
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
        *version = local_version;
    }

    return local_candidate_found;
}
