// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_BACKOFF_H
#define GGL_BACKOFF_H

//! Greengrass backoff util

#include <ggl/error.h>
#include <stdint.h>

GglError ggl_backoff(
    uint32_t base_ms,
    uint32_t max_ms,
    uint32_t max_attempts,
    GglError (*fn)(void *ctx),
    void *ctx
);

void ggl_backoff_indefinite(
    uint32_t base_ms, uint32_t max_ms, GglError (*fn)(void *ctx), void *ctx
);

#endif
