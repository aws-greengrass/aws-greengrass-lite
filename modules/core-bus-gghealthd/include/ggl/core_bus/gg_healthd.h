// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_CORE_BUS_GG_HEALTHD_H
#define GGL_CORE_BUS_GG_HEALTHD_H

//! gghealthd core-bus interface wrapper

#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/error.h>

GglError ggl_gghealthd_retrieve_component_status(
    GglBuffer component, GglArena *alloc, GglBuffer *component_status
);

#endif
