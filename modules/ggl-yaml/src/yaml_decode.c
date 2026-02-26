// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <gg/arena.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <ggl/yaml_decode.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <yaml.h>
#include <stdint.h>

static GgError yaml_to_obj(
    yaml_document_t *document, yaml_node_t *node, GgArena *arena, GgObject *obj
);

static GgError yaml_node_to_buf(yaml_node_t *node, GgBuffer *buf) {
    assert(node != NULL);
    assert(node->type == YAML_SCALAR_NODE);

    uint8_t *value = node->data.scalar.value;
    size_t len = strlen((char *) value);

    if (buf != NULL) {
        *buf = (GgBuffer) { .data = value, .len = len };
    }
    return GG_ERR_OK;
}

static GgError yaml_scalar_to_obj(yaml_node_t *node, GgObject *obj) {
    assert(node != NULL);
    assert(node->type == YAML_SCALAR_NODE);

    GgBuffer result;
    GgError ret = yaml_node_to_buf(node, &result);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    if (obj != NULL) {
        *obj = gg_obj_buf(result);
    }
    return GG_ERR_OK;
}

// NOLINTNEXTLINE(misc-no-recursion)
static GgError yaml_mapping_to_obj(
    yaml_document_t *document, yaml_node_t *node, GgArena *arena, GgObject *obj
) {
    assert(document != NULL);
    assert(node != NULL);
    assert(node->type == YAML_MAPPING_NODE);
    assert(arena != NULL);

    if (node->data.mapping.pairs.top < node->data.mapping.pairs.start) {
        GG_LOGE("Unexpected result from libyaml.");
        return GG_ERR_FAILURE;
    }

    size_t len = (size_t) (node->data.mapping.pairs.top
                           - node->data.mapping.pairs.start);

    if (len == 0) {
        if (obj != NULL) {
            *obj = gg_obj_map((GgMap) { 0 });
        }
        return GG_ERR_OK;
    }

    GgKV *pairs = NULL;
    if (obj != NULL) {
        pairs = GG_ARENA_ALLOCN(arena, GgKV, len);
        if (pairs == NULL) {
            GG_LOGE("Insufficent memory to decode yaml.");
            return GG_ERR_NOMEM;
        }
    }

    yaml_node_pair_t *node_pairs = node->data.mapping.pairs.start;
    for (size_t i = 0; i < len; i++) {
        yaml_node_t *key_node
            = yaml_document_get_node(document, node_pairs[i].key);
        if (key_node == NULL) {
            GG_LOGE("Yaml mapping key NULL.");
            return GG_ERR_FAILURE;
        }
        yaml_node_t *value_node
            = yaml_document_get_node(document, node_pairs[i].value);
        if (value_node == NULL) {
            GG_LOGE("Yaml mapping value NULL.");
            return GG_ERR_FAILURE;
        }

        if (key_node->type != YAML_SCALAR_NODE) {
            GG_LOGE("Yaml mapping key not a scalar.");
            return GG_ERR_FAILURE;
        }

        GgError ret;
        if (pairs == NULL) {
            ret = yaml_node_to_buf(key_node, NULL);
        } else {
            GgBuffer key = { 0 };
            ret = yaml_node_to_buf(key_node, &key);
            gg_kv_set_key(&pairs[i], key);
        }
        if (ret != GG_ERR_OK) {
            return ret;
        }

        GgObject *val = (pairs == NULL) ? NULL : gg_kv_val(&pairs[i]);

        ret = yaml_to_obj(document, value_node, arena, val);
        if (ret != GG_ERR_OK) {
            return ret;
        }
    }

    if (obj != NULL) {
        *obj = gg_obj_map((GgMap) { .pairs = pairs, .len = len });
    }
    return GG_ERR_OK;
}

// NOLINTNEXTLINE(misc-no-recursion)
static GgError yaml_sequence_to_obj(
    yaml_document_t *document, yaml_node_t *node, GgArena *arena, GgObject *obj
) {
    assert(document != NULL);
    assert(node != NULL);
    assert(node->type == YAML_SEQUENCE_NODE);
    assert(arena != NULL);

    if (node->data.sequence.items.top < node->data.sequence.items.start) {
        GG_LOGE("Unexpected result from libyaml.");
        return GG_ERR_FAILURE;
    }

    size_t len = (size_t) (node->data.sequence.items.top
                           - node->data.sequence.items.start);

    if (len == 0) {
        if (obj != NULL) {
            *obj = gg_obj_list((GgList) { 0 });
        }
        return GG_ERR_OK;
    }

    GgObject *items = NULL;
    if (obj != NULL) {
        items = GG_ARENA_ALLOCN(arena, GgObject, len);
        if (items == NULL) {
            GG_LOGE("Insufficent memory to decode yaml.");
            return GG_ERR_NOMEM;
        }
    }

    yaml_node_item_t *item_nodes = node->data.sequence.items.start;
    for (size_t i = 0; i < len; i++) {
        yaml_node_t *item_node
            = yaml_document_get_node(document, item_nodes[i]);
        if (item_node == NULL) {
            GG_LOGE("Yaml sequence node NULL.");
            return GG_ERR_FAILURE;
        }

        GgObject *item = (items == NULL) ? NULL : &items[i];

        GgError ret = yaml_to_obj(document, item_node, arena, item);
        if (ret != GG_ERR_OK) {
            return ret;
        }
    }

    if (obj != NULL) {
        *obj = gg_obj_list((GgList) { .items = items, .len = len });
    }
    return GG_ERR_OK;
}

// NOLINTNEXTLINE(misc-no-recursion)
static GgError yaml_to_obj(
    yaml_document_t *document, yaml_node_t *node, GgArena *arena, GgObject *obj
) {
    assert(document != NULL);
    assert(node != NULL);
    assert(arena != NULL);

    switch (node->type) {
    case YAML_NO_NODE: {
        GG_LOGE("Unexpected missing node from libyaml.");
        return GG_ERR_FAILURE;
    }
    case YAML_SCALAR_NODE:
        return yaml_scalar_to_obj(node, obj);
    case YAML_MAPPING_NODE:
        return yaml_mapping_to_obj(document, node, arena, obj);
    case YAML_SEQUENCE_NODE:
        return yaml_sequence_to_obj(document, node, arena, obj);
    }

    GG_LOGE("Unexpected node type from libyaml.");
    return GG_ERR_FAILURE;
}

GgError ggl_yaml_decode_destructive(
    GgBuffer buf, GgArena *arena, GgObject *obj
) {
    static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    GG_MTX_SCOPE_GUARD(&mtx);

    GG_LOGT(
        "%s received yaml content: %.*s", __func__, (int) buf.len, buf.data
    );

    static yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser)) {
        GG_LOGE("Parser initialization failed.");
        return GG_ERR_FATAL;
    }
    yaml_parser_set_input_string(&parser, buf.data, buf.len);
    static yaml_document_t document;
    if (!yaml_parser_load(&parser, &document)) {
        GG_LOGE(
            "Yaml parser load failed. Parser error: %s, at line %zu, column %zu",
            parser.problem,
            parser.problem_mark.line + 1,
            parser.problem_mark.column + 1
        );
        yaml_parser_delete(&parser);
        return GG_ERR_PARSE;
    }
    yaml_node_t *root_node = yaml_document_get_root_node(&document);
    if (root_node == NULL) {
        GG_LOGE("Yaml document is empty.");
        return GG_ERR_NOENTRY;
    }

    // Handle NULL arena arg
    GgArena empty_arena = { 0 };
    GgArena *result_arena = (arena == NULL) ? &empty_arena : arena;

    // Copy to avoid committing allocation on error path
    GgArena arena_copy = *result_arena;

    GgError ret = yaml_to_obj(&document, root_node, &arena_copy, obj);

    if (obj != NULL) {
        if (ret == GG_ERR_OK) {
            // Copy buffers (dynamically allocated by libyaml) into buf to mimic
            // in-place buffer decoding
            GgArena buf_arena = gg_arena_init(buf);
            ret = gg_arena_claim_obj_bufs(obj, &buf_arena);
        }

        if (ret == GG_ERR_OK) {
            // Commit allocations
            *result_arena = arena_copy;
        }
    }

    yaml_document_delete(&document);
    yaml_parser_delete(&parser);

    return ret;
}
