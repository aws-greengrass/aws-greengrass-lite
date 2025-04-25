// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_FLAGS_H
#define GGL_FLAGS_H

//! Generic named flag types

#include <ggl/attr.h>

typedef struct DESIGNATED_INIT {
    enum {
        GGL_PRESENCE_REQUIRED,
        GGL_PRESENCE_OPTIONAL,
        GGL_PRESENCE_MISSING
    } val;
} GglPresence;

static const GglPresence GGL_REQUIRED = { .val = GGL_PRESENCE_REQUIRED };
static const GglPresence GGL_OPTIONAL = { .val = GGL_PRESENCE_OPTIONAL };
static const GglPresence GGL_MISSING = { .val = GGL_PRESENCE_MISSING };

#endif
