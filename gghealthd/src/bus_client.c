// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "bus_client.h"
#include <sys/types.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <pthread.h>
#include <string.h>
#include <stdint.h>

static pthread_mutex_t bump_alloc_mutex = PTHREAD_MUTEX_INITIALIZER;

// Check a component's version field in ggconfigd for proof of existence
GglError verify_component_exists(GglBuffer component_name) {
    if ((component_name.data == NULL) || (component_name.len == 0)
        || (component_name.len > 128U)) {
        return GGL_ERR_RANGE;
    }

    int ret = pthread_mutex_lock(&bump_alloc_mutex);
    if (ret < 0) {
        return GGL_ERR_FAILURE;
    }
    GGL_DEFER(pthread_mutex_unlock, bump_alloc_mutex);

    static uint8_t component_version_mem[512] = { 0 };
    GglBuffer component_version = GGL_BUF(component_version_mem);
    GglError config_ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("services"), component_name, GGL_STR("version")),
        &component_version
    );

    if (config_ret != GGL_ERR_OK) {
        GGL_LOGE("failed to connect to ggconfigd");
        return config_ret;
    }
    GGL_LOGD(
        "Component version read as %.*s",
        (int) component_version.len,
        component_version.data
    );
    return GGL_ERR_OK;
}
