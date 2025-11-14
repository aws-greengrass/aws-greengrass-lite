// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "bus_client.h"
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/log.h>
#include <ggl/core_bus/gg_config.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>

static pthread_mutex_t bump_alloc_mutex = PTHREAD_MUTEX_INITIALIZER;

// Check a component's version field in ggconfigd for proof of existence
GgError verify_component_exists(GgBuffer component_name) {
    // Remove .install and .bootstrap if at the end of the component name
    if (gg_buffer_has_suffix(component_name, GG_STR(".install"))) {
        component_name = gg_buffer_substr(
            component_name, 0, component_name.len - GG_STR(".install").len
        );
    }
    if (gg_buffer_has_suffix(component_name, GG_STR(".bootstrap"))) {
        component_name = gg_buffer_substr(
            component_name, 0, component_name.len - GG_STR(".bootstrap").len
        );
    }

    if ((component_name.data == NULL) || (component_name.len == 0)
        || (component_name.len > 128U)) {
        return GG_ERR_RANGE;
    }

    GG_MTX_SCOPE_GUARD(&bump_alloc_mutex);

    GgArena alloc = gg_arena_init(GG_BUF((uint8_t[512]) { 0 }));
    GgBuffer component_version;
    GgError config_ret = ggl_gg_config_read_str(
        GG_BUF_LIST(GG_STR("services"), component_name, GG_STR("version")),
        &alloc,
        &component_version
    );

    if (config_ret != GG_ERR_OK) {
        GG_LOGE("failed to connect to ggconfigd");
        return config_ret;
    }
    GG_LOGD(
        "Component version read as %.*s",
        (int) component_version.len,
        component_version.data
    );
    return GG_ERR_OK;
}

GgError get_root_component_list(GgArena *alloc, GgList *component_names) {
    return ggl_gg_config_list(
        GG_BUF_LIST(GG_STR("services")), alloc, component_names
    );
}

bool is_nucleus_component_type(GgBuffer component_name) {
    GgArena alloc = gg_arena_init(GG_BUF((uint8_t[32]) { 0 }));
    GgBuffer component_type;
    GgError ret = ggl_gg_config_read_str(
        GG_BUF_LIST(
            GG_STR("services"), component_name, GG_STR("componentType")
        ),
        &alloc,
        &component_type
    );
    if (ret != GG_ERR_OK) {
        return false;
    }
    return gg_buffer_eq(GG_STR("NUCLEUS"), component_type);
}
