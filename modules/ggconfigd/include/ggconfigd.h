// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGCONFIGD_H
#define GGCONFIGD_H

#include <gg/error.h>
#include <gg/types.h>
#include <gg/vector.h>
#include <stdint.h>

// TODO: we could save this static memory by having json decoding done as we
// read each object in the db_interface layer.
// For now, set to something slightly smaller than GGCONFIGD_MAX_DB_READ_BYTES
#define GGCONFIGD_MAX_OBJECT_DECODE_BYTES 9000

GgError ggconfig_write_value_at_key(
    GgList *key_path, GgBuffer *value, int64_t timestamp
);
GgError ggconfig_write_empty_map(GgList *key_path);
GgError ggconfig_delete_key(GgList *key_path);
GgError ggconfig_get_value_from_key(GgList *key_path, GgObject *value);
GgError ggconfig_list_subkeys(GgList *key_path, GgList *subkeys);
GgError ggconfig_get_key_notification(GgList *key_path, uint32_t handle);
GgError ggconfig_open(void);
GgError ggconfig_close(void);

void ggconfigd_start_server(void);

GgError ggconfig_load_file(GgBuffer path);
GgError ggconfig_load_dir(GgBuffer path);

GgError ggconfig_process_nonmap(
    GgObjVec *key_path, GgObject value, int64_t timestamp
);
GgError ggconfig_process_map(GgObjVec *key_path, GgMap map, int64_t timestamp);

#endif
