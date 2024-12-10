// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "component_manager.h"
#include "component_store.h"
#include <assert.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/core_bus/gg_healthd.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/semver.h>
#include <limits.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#define LOCAL_DEPLOYMENT "LOCAL_DEPLOYMENT"

static GglError find_active_version(
    GglBuffer package_name, GglBuffer version_requirement, GglBuffer *version
) {
    // check the config to see if the provided package name is already a running
    // service

    // find the version of the active running component
    static uint8_t version_resp_mem[128] = { 0 };
    GglBuffer version_resp = GGL_BUF(version_resp_mem);
    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("services"), package_name, GGL_STR("version")),
        &version_resp
    );

    if (ret != GGL_ERR_OK) {
        GGL_LOGI(
            "Unable to retrieve version of %.*s. Assuming no active version "
            "found.",
            (int) package_name.len,
            package_name.data
        );
        return GGL_ERR_NOENTRY;
    }

    // active component found, update the version if it is a valid version
    if (!is_in_range(version_resp, version_requirement)) {
        return GGL_ERR_NOENTRY;
    }

    // Check that the component is actually running (or finished)
    uint8_t component_status_buf[NAME_MAX];
    GglBuffer component_status = GGL_BUF(component_status_buf);
    ret = ggl_gghealthd_retrieve_component_status(
        package_name, &component_status
    );

    if (ret != GGL_ERR_OK) {
        GGL_LOGI(
            "Component status not found for component %.*s despite finding "
            "active version. Not using this version.",
            (int) package_name.len,
            package_name.data
        );
        return GGL_ERR_INVALID;
    }

    if (!ggl_buffer_eq(component_status, GGL_STR("RUNNING"))
        && !ggl_buffer_eq(component_status, GGL_STR("FINISHED"))) {
        GGL_LOGI(
            "Component %.*s is not in the RUNNING or FINISHED states. Not "
            "using the active version.",
            (int) package_name.len,
            package_name.data
        );
        return GGL_ERR_INVALID;
    }

    *version = version_resp;
    return GGL_ERR_OK;
}

static GglError find_best_candidate_locally(
    GglBuffer component_name, GglBuffer version_requirement, GglBuffer *version
) {
    GGL_LOGD("Searching for the best local candidate on the device.");

    GglError ret
        = find_active_version(component_name, version_requirement, version);

    if (ret == GGL_ERR_OK) {
        GGL_LOGI("Found running component which meets the version requirements."
        );
        return GGL_ERR_OK;
    }
    GGL_LOGI("No running component satisfies the version requirements. "
             "Searching in the local component store.");

    return find_available_component(
        component_name, version_requirement, version
    );
}

bool resolve_component_version(
    GglBuffer component_name,
    GglBuffer version_requirement,
    GglBuffer *resolved_version
) {
    GGL_LOGD("Resolving component version.");

    // find best local candidate
    uint8_t local_version_arr[NAME_MAX];
    GglBuffer local_version = GGL_BUF(local_version_arr);
    GglError ret = find_best_candidate_locally(
        component_name, version_requirement, &local_version
    );

    if (ret != GGL_ERR_OK) {
        GGL_LOGI(
            "Failed to find a local candidate that satisfies the requrement."
        );
        return false;
    }

    // TODO: also check that the component region matches the expected region
    // (component store functionality)
    GGL_LOGI(
        "Found local candidate for %.*s that satisfies version requirements. "
        "Using "
        "the local candidate as the resolved version "
        "without negotiating with the cloud.",
        (int) component_name.len,
        (char *) component_name.data
    );

    assert(local_version.len <= NAME_MAX);
    memcpy(resolved_version->data, local_version.data, local_version.len);
    resolved_version->len = local_version.len;
    return true;
}
