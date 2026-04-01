// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "gg-lite-health-daemon/config.h"
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/list.h>
#include <gg/log.h>
#include <gg/object.h>
#include <gg/types.h>
#include <ggl/core_bus/gg_config.h>
#include <string.h>
#include <stdint.h>

static const HealthDaemonConfig DEFAULT_CONFIG = {
    .output_mode = "emf",
    .output_directory = "/var/log/gg-metrics/system-health/",
    .collection_interval_sec = 60,
    .proc_path = "/proc",
    .sys_path = "/sys",
    .exclude_fstypes = { "tmpfs", "proc", "sysfs", "devtmpfs" },
    .exclude_fstype_count = 4,
    .exclude_interfaces = { "lo" },
    .exclude_interface_count = 1,
};

GgError config_init(HealthDaemonConfig *config) {
    *config = DEFAULT_CONFIG;
    return GG_ERR_OK;
}

static void copy_buf(GgBuffer src, char *dest, size_t max_len) {
    if (max_len == 0) {
        return;
    }
    size_t len = (src.len < max_len - 1) ? src.len : max_len - 1;
    memcpy(dest, src.data, len);
    dest[len] = '\0';
}

static GgError read_config_field(
    const char *field, GgArena *alloc, GgObject *obj
) {
    return ggl_gg_config_read(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("aws.greengrass.NucleusLite"),
            GG_STR("configuration"),
            GG_STR("healthDaemon"),
            gg_buffer_from_null_term((char *) field)
        ),
        alloc,
        obj
    );
}

static void load_str(const char *field, char *dest, size_t max_len) {
    uint8_t mem[256];
    GgArena alloc = gg_arena_init(GG_BUF(mem));
    GgObject obj;
    if (read_config_field(field, &alloc, &obj) == GG_ERR_OK
        && gg_obj_type(obj) == GG_TYPE_BUF) {
        copy_buf(gg_obj_into_buf(obj), dest, max_len);
    }
}

static void load_u32(const char *field, uint32_t *dest) {
    uint8_t mem[64];
    GgArena alloc = gg_arena_init(GG_BUF(mem));
    GgObject obj;
    if (read_config_field(field, &alloc, &obj) == GG_ERR_OK
        && gg_obj_type(obj) == GG_TYPE_I64) {
        int64_t val = gg_obj_into_i64(obj);
        // Reject zero and negative; only positive values override defaults.
        if (val > 0 && val <= UINT32_MAX) {
            *dest = (uint32_t) val;
        }
    }
}

static void load_str_list(
    const char *field, char dest[][MAX_EXCLUDE_ENTRY_LEN], size_t *count
) {
    uint8_t mem[2048];
    GgArena alloc = gg_arena_init(GG_BUF(mem));
    GgObject obj;
    if (read_config_field(field, &alloc, &obj) != GG_ERR_OK
        || gg_obj_type(obj) != GG_TYPE_LIST) {
        return;
    }
    GgList obj_list = gg_obj_into_list(obj);
    size_t n = 0;
    GG_LIST_FOREACH (item, obj_list) {
        if (n >= MAX_EXCLUDE_ENTRIES) {
            GG_LOGW("Exclude list full, dropping entry");
            break;
        }
        if (gg_obj_type(*item) == GG_TYPE_BUF) {
            copy_buf(gg_obj_into_buf(*item), dest[n], MAX_EXCLUDE_ENTRY_LEN);
            n++;
        }
    }
    *count = n;
}

GgError config_load(HealthDaemonConfig *config) {
    load_str("outputMode", config->output_mode, MAX_LABEL_LEN);
    load_str("outputDirectory", config->output_directory, MAX_PATH_LEN);
    load_u32("collectionIntervalSec", &config->collection_interval_sec);
    load_str("procPath", config->proc_path, MAX_PATH_LEN);
    load_str("sysPath", config->sys_path, MAX_PATH_LEN);
    load_str_list(
        "excludeMounts", config->exclude_fstypes, &config->exclude_fstype_count
    );
    load_str_list(
        "excludeInterfaces",
        config->exclude_interfaces,
        &config->exclude_interface_count
    );

    return GG_ERR_OK;
}
