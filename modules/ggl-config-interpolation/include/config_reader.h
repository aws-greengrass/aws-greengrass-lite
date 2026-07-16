// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_CONFIG_READER_H
#define GGL_CONFIG_READER_H

#include <gg/error.h>
#include <gg/io.h>
#include <gg/types.h>
#include <stddef.h>

/// Abstract config reader
/// Retrieves the config value of a key and submits it to a receiver
typedef struct {
    GgError (*reader)(void *ctx, GgBuffer key, GgObjectReceiver receiver);
    void *ctx;
} GgConfigReader;

static inline GgError ggl_config_reader_call(
    GgConfigReader reader, GgBuffer key, GgObjectReceiver receiver
) {
    if (reader.reader == NULL) {
        return GG_ERR_INVALID;
    }
    return reader.reader(reader.ctx, key, receiver);
}

// Reader that no config values can be read from.
#define GGL_CONFIG_NULL_READER (GgConfigReader) { 0 }

#endif
