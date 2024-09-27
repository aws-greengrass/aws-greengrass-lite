// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_CORE_BUS_GG_CONFIG_H
#define GGL_CORE_BUS_GG_CONFIG_H

//! gg_config core-bus interface wrapper

#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/object.h>
#include <stdint.h>

#define GGL_MAX_CONFIG_DEPTH 10

/// Wrapper for core-bus `gg_config` `read`
GglError ggl_gg_config_read(
    GglBufList key_path, GglAlloc *alloc, GglObject *result
);

/// Get string from core-bus `gg_config` `read`
/// `result` must point to buffer with memory to read into
GglError ggl_gg_config_read_str(GglBufList key_path, GglBuffer *result);

/// Wrapper for core-bus `gg_config` `write`
GglError ggl_gg_config_write(
    GglBufList key_path, GglObject value, int64_t timestamp
);

/// Wrapper for core-bus `gg_config` `subscribe`
GglError ggl_gg_config_subscribe(
    GglBufList key_path,
    GglSubscribeCallback on_response,
    GglSubscribeCloseCallback on_close,
    void *ctx,
    uint32_t *handle
);

#endif
