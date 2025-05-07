// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <ggl/attr.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/flags.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

// This assumption is used to size buffers in a lot of places
static_assert(
    sizeof(GglKV) <= 2 * sizeof(GglObject),
    "GglKV must be at most the size of two GglObjects."
);

COLD static void length_err(size_t *len) {
    GGL_LOGE(
        "Key length longer than can be stored in GglKV (%zu, max %u).",
        *len,
        (unsigned int) UINT16_MAX
    );
    assert(false);
    *len = UINT16_MAX;
}

GglKV ggl_kv(GglBuffer key, GglObject val) {
    GglKV result = { 0 };

    static_assert(
        sizeof(result._private) >= sizeof(void *) + 2 + sizeof(GglObject),
        "GglKV must be able to hold key pointer, 16-bit key length, and value."
    );

    ggl_kv_set_key(&result, key);

    memcpy(&result._private[sizeof(void *) + 2], &val, sizeof(GglObject));
    return result;
}

GglBuffer ggl_kv_key(GglKV kv) {
    void *ptr;
    uint16_t len;
    memcpy(&ptr, kv._private, sizeof(void *));
    memcpy(&len, &kv._private[sizeof(void *)], 2);
    return (GglBuffer) { .data = ptr, .len = len };
}

void ggl_kv_set_key(GglKV *kv, GglBuffer key) {
    if (key.len > UINT16_MAX) {
        length_err(&key.len);
    }
    uint16_t key_len = (uint16_t) key.len;
    memcpy(kv->_private, &key.data, sizeof(void *));
    memcpy(&kv->_private[sizeof(void *)], &key_len, 2);
}

GglObject *ggl_kv_val(GglKV *kv) {
    return (GglObject *) &kv->_private[sizeof(void *) + 2];
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
