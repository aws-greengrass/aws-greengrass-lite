// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "component_manager.h"
#include "component_store.h"
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/core_bus/gg_config.h>
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
    // check the config to see if the provided package name is already a running
    // service
    static uint8_t resp_mem[128] = { 0 };
    GglBuffer resp = GGL_BUF(resp_mem);
    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("services"), package_name), &resp
    );

    if (ret != GGL_ERR_OK) {
        GGL_LOGI(
            "component-manager",
            "No active running version of %s found.",
            package_name.data
        );
        return GGL_ERR_NOENTRY;
    }

    // find the version of the active running component
    static uint8_t version_resp_mem[128] = { 0 };
    GglBuffer version_resp = GGL_BUF(version_resp_mem);
    ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("services"), resp, GGL_STR("version")),
        &version_resp
    );

    if (ret != GGL_ERR_OK) {
        GGL_LOGI(
            "component-manager",
            "Unable to retrieve version of %s. Assuming no active version "
            "found.",
            package_name.data
        );
        return GGL_ERR_NOENTRY;
    }

    // active component found, update the version
    *version = version_resp;
    return GGL_ERR_OK;
}

static GglError find_best_candidate_locally(
    GglBuffer component_name, GglBuffer version_requirement, GglBuffer *version
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

        find_available_component(component_name, version_requirement, version);
    }

    return GGL_ERR_OK;
}

bool resolve_component_version(
    GglBuffer component_name,
    GglBuffer version_requirement,
    GglBuffer *resolved_version
) {
    GGL_LOGD("component-manager", "Resolving component version.");

    // find best local candidate
    GglBuffer local_version;
    GglError ret = find_best_candidate_locally(
        component_name, version_requirement, &local_version
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

    // TODO: also check that the component region matches the expected region
    // (component store functionality)
    if (local_candidate_found) {
        GGL_LOGI(
            "component-manager",
            "Found local candidate that satisfies version requirements. Using "
            "the local candidate as the resolved version "
            "without negotiating with the cloud."
        );
        *resolved_version = local_version;
    }

    return local_candidate_found;
}
