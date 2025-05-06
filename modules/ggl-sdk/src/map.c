// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/flags.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    GglBuffer key;
    GglObject val;
} GglKVPriv;

static_assert(sizeof(GglKV) == sizeof(GglKVPriv), "GglKV impl invalid.");
static_assert(alignof(GglKV) == alignof(GglKVPriv), "GglKV impl invalid.");

GglKV ggl_kv(GglBuffer key, GglObject val) {
    union {
        GglKVPriv priv;
        GglKV pub;
    } kv = { .priv = { .key = key, .val = val } };

    return kv.pub;
}

GglBuffer ggl_kv_key(GglKV kv) {
    return ((GglKVPriv *) &kv)->key;
}

void ggl_kv_set_key(GglKV *kv, GglBuffer key) {
    ((GglKVPriv *) kv)->key = key;
}

GglObject *ggl_kv_val(GglKV *kv) {
    return &((GglKVPriv *) kv)->val;
}

bool ggl_map_get(GglMap map, GglBuffer key, GglObject **result) {
    GGL_MAP_FOREACH(pair, map) {
        if (ggl_buffer_eq(key, ggl_kv_key(*pair))) {
            if (result != NULL) {
                *result = ggl_kv_val(pair);
            }
            return true;
        }
    }
    if (result != NULL) {
        *result = NULL;
    }
    return false;
}

GglError ggl_map_validate(GglMap map, GglMapSchema schema) {
    for (size_t i = 0; i < schema.entry_count; i++) {
        GglMapSchemaEntry *entry = &schema.entries[i];
        GglObject *value;
        bool found = ggl_map_get(map, entry->key, &value);
        if (!found) {
            if (entry->required.val == GGL_PRESENCE_REQUIRED) {
                GGL_LOGE(
                    "Map missing required key %.*s.",
                    (int) entry->key.len,
                    entry->key.data
                );
                return GGL_ERR_NOENTRY;
            }

            if (entry->required.val == GGL_PRESENCE_OPTIONAL) {
                GGL_LOGT(
                    "Missing optional key %.*s.",
                    (int) entry->key.len,
                    entry->key.data
                );
            }

            if (entry->value != NULL) {
                *entry->value = NULL;
            }
            continue;
        }

        GGL_LOGT(
            "Found key %.*s with len %zu",
            (int) entry->key.len,
            entry->key.data,
            entry->key.len
        );

        if (entry->required.val == GGL_PRESENCE_MISSING) {
            GGL_LOGE(
                "Map has required missing key %.*s.",
                (int) entry->key.len,
                entry->key.data
            );
            return GGL_ERR_PARSE;
        }

        if (entry->type != GGL_TYPE_NULL) {
            if (entry->type != ggl_obj_type(*value)) {
                GGL_LOGE(
                    "Key %.*s is of invalid type.",
                    (int) entry->key.len,
                    entry->key.data
                );
                return GGL_ERR_PARSE;
            }
        }

        if (entry->value != NULL) {
            *entry->value = value;
        }
    }

    return GGL_ERR_OK;
}
