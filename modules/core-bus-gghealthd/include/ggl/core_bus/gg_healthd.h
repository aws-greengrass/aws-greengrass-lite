// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_CORE_BUS_GG_HEALTHD_H
#define GGL_CORE_BUS_GG_HEALTHD_H

//! gghealthd core-bus interface wrapper

#include <gg/arena.h>
#include <gg/error.h>
#include <gg/types.h>
#include <ggl/core_bus/client.h>
#include <stdint.h>

GgError ggl_gghealthd_retrieve_component_status(
    GgBuffer component, GgArena *alloc, GgBuffer *component_status
);

/// Subscribe to lifecycle state changes of every Greengrass (`ggl.*`)
/// component. `on_response` receives a map with `component_name` and
/// `lifecycle_state` buffers for each terminal-state change. Wraps the
/// `gg_health` `subscribe_to_all_component_state_changes` core-bus method.
GgError ggl_gghealthd_subscribe_to_all_component_state_changes(
    GglSubscribeCallback on_response,
    GglSubscribeCloseCallback on_close,
    void *ctx,
    GgError *remote_error,
    uint32_t *handle
);

#endif
