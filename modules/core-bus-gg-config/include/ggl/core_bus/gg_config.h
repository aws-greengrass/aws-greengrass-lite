// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_CORE_BUS_GG_CONFIG_H
#define GGL_CORE_BUS_GG_CONFIG_H

//! gg_config core-bus interface wrapper

#include <gg/arena.h>
#include <gg/error.h>
#include <gg/types.h>
#include <ggl/core_bus/client.h>
#include <stdint.h>

/// Wrapper for core-bus `gg_config` `read`
GgError ggl_gg_config_read(
    GgBufList key_path, GgArena *alloc, GgObject *result
);

/// Get string from core-bus `gg_config` `read`
GgError ggl_gg_config_read_str(
    GgBufList key_path, GgArena *alloc, GgBuffer *result
);

/// Wrapper for core-bus `gg_config` `list`
// subkeys_out is a list of buffer objects.
GgError ggl_gg_config_list(
    GgBufList key_path, GgArena *alloc, GgList *subkeys_out
);

/// Wrapper for core-bus `gg_config` `write`
GgError ggl_gg_config_write(
    GgBufList key_path, GgObject value, const int64_t *timestamp
);

/// Wrapper for core-bus `gg_config` `delete`
GgError ggl_gg_config_delete(GgBufList key_path);

/// Wrapper for core-bus `gg_config` `subscribe`
GgError ggl_gg_config_subscribe(
    GgBufList key_path,
    GglSubscribeCallback on_response,
    GglSubscribeCloseCallback on_close,
    void *ctx,
    uint32_t *handle
);

#endif
