// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGCONFIGD_H
#define GGCONFIGD_H

#include <ggl/error.h>
#include <ggl/object.h>
#include <stdint.h>

// TODO: we could save this static memory by having json decoding done as we
// read each object in the db_interface layer.
// For now, set to something slightly smaller than GGCONFIGD_MAX_DB_READ_BYTES
#define GGCONFIGD_MAX_OBJECT_DECODE_BYTES 9000

GglError ggconfig_write_value_at_key(
    GglList *key_path, GglBuffer *value, int64_t timestamp
);
GglError ggconfig_get_value_from_key(GglList *key_path, GglObject *value);
GglError ggconfig_get_key_notification(GglList *key_path, uint32_t handle);
GglError ggconfig_open(void);
GglError ggconfig_close(void);

void ggconfigd_start_server(void);

#endif
