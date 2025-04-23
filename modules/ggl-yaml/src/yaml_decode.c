// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/yaml_decode.h"
#include "pthread.h"
#include <assert.h>
#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/cleanup.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <string.h>
#include <sys/types.h>
#include <yaml.h>
#include <stdint.h>

static GglError yaml_to_obj(
    yaml_document_t *document,
    yaml_node_t *node,
    GglArena *arena,
    GglObject *obj
);

static GglError yaml_node_to_buf(yaml_node_t *node, GglBuffer *buf) {
    assert(node != NULL);
    assert(node->type == YAML_SCALAR_NODE);

    uint8_t *value = node->data.scalar.value;
    size_t len = strlen((char *) value);

    if (buf != NULL) {
        *buf = (GglBuffer) { .data = value, .len = len };
    }
    return GGL_ERR_OK;
}

static GglError yaml_scalar_to_obj(yaml_node_t *node, GglObject *obj) {
    assert(node != NULL);
    assert(node->type == YAML_SCALAR_NODE);

    GglBuffer result;
    GglError ret = yaml_node_to_buf(node, &result);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (obj != NULL) {
        *obj = ggl_obj_buf(result);
    }
    return GGL_ERR_OK;
}

// NOLINTNEXTLINE(misc-no-recursion)
static GglError yaml_mapping_to_obj(
    yaml_document_t *document,
    yaml_node_t *node,
    GglArena *arena,
    GglObject *obj
) {
    assert(document != NULL);
    assert(node != NULL);
    assert(node->type == YAML_MAPPING_NODE);
    assert(arena != NULL);

    if (node->data.mapping.pairs.top < node->data.mapping.pairs.start) {
        GGL_LOGE("Unexpected result from libyaml.");
        return GGL_ERR_FAILURE;
    }

    size_t len = (size_t) (node->data.mapping.pairs.top
                           - node->data.mapping.pairs.start);

    if (len == 0) {
        if (obj != NULL) {
            *obj = ggl_obj_map((GglMap) { 0 });
        }
        return GGL_ERR_OK;
    }

    GglKV *pairs = NULL;
    if (obj != NULL) {
        pairs = GGL_ARENA_ALLOCN(arena, GglKV, len);
        if (pairs == NULL) {
            GGL_LOGE("Insufficent memory to decode yaml.");
            return GGL_ERR_NOMEM;
        }
    }

    yaml_node_pair_t *node_pairs = node->data.mapping.pairs.start;
    for (size_t i = 0; i < len; i++) {
        yaml_node_t *key_node
            = yaml_document_get_node(document, node_pairs[i].key);
        if (key_node == NULL) {
            GGL_LOGE("Yaml mapping key NULL.");
            return GGL_ERR_FAILURE;
        }
        yaml_node_t *value_node
            = yaml_document_get_node(document, node_pairs[i].value);
        if (value_node == NULL) {
            GGL_LOGE("Yaml mapping value NULL.");
            return GGL_ERR_FAILURE;
        }

        if (key_node->type != YAML_SCALAR_NODE) {
            GGL_LOGE("Yaml mapping key not a scalar.");
            return GGL_ERR_FAILURE;
        }

        GglBuffer *key = (pairs == NULL) ? NULL : &pairs[i].key;
        GglObject *val = (pairs == NULL) ? NULL : &pairs[i].val;

        GglError ret = yaml_node_to_buf(key_node, key);
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        ret = yaml_to_obj(document, value_node, arena, val);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    if (obj != NULL) {
        *obj = ggl_obj_map((GglMap) { .pairs = pairs, .len = len });
    }
    return GGL_ERR_OK;
}

// NOLINTNEXTLINE(misc-no-recursion)
static GglError yaml_sequence_to_obj(
    yaml_document_t *document,
    yaml_node_t *node,
    GglArena *arena,
    GglObject *obj
) {
    assert(document != NULL);
    assert(node != NULL);
    assert(node->type == YAML_SEQUENCE_NODE);
    assert(arena != NULL);

    if (node->data.sequence.items.top < node->data.sequence.items.start) {
        GGL_LOGE("Unexpected result from libyaml.");
        return GGL_ERR_FAILURE;
    }

    size_t len = (size_t) (node->data.sequence.items.top
                           - node->data.sequence.items.start);

    if (len == 0) {
        if (obj != NULL) {
            *obj = ggl_obj_list((GglList) { 0 });
        }
        return GGL_ERR_OK;
    }

    GglObject *items = NULL;
    if (obj != NULL) {
        items = GGL_ARENA_ALLOCN(arena, GglObject, len);
        if (items == NULL) {
            GGL_LOGE("Insufficent memory to decode yaml.");
            return GGL_ERR_NOMEM;
        }
    }

    yaml_node_item_t *item_nodes = node->data.sequence.items.start;
    for (size_t i = 0; i < len; i++) {
        yaml_node_t *item_node
            = yaml_document_get_node(document, item_nodes[i]);
        if (item_node == NULL) {
            GGL_LOGE("Yaml sequence node NULL.");
            return GGL_ERR_FAILURE;
        }

        GglObject *item = (items == NULL) ? NULL : &items[i];

        GglError ret = yaml_to_obj(document, item_node, arena, item);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    if (obj != NULL) {
        *obj = ggl_obj_list((GglList) { .items = items, .len = len });
    }
    return GGL_ERR_OK;
}

// NOLINTNEXTLINE(misc-no-recursion)
static GglError yaml_to_obj(
    yaml_document_t *document,
    yaml_node_t *node,
    GglArena *arena,
    GglObject *obj
) {
    assert(document != NULL);
    assert(node != NULL);
    assert(arena != NULL);

    switch (node->type) {
    case YAML_NO_NODE: {
        GGL_LOGE("Unexpected missing node from libyaml.");
        return GGL_ERR_FAILURE;
    }
    case YAML_SCALAR_NODE:
        return yaml_scalar_to_obj(node, obj);
    case YAML_MAPPING_NODE:
        return yaml_mapping_to_obj(document, node, arena, obj);
    case YAML_SEQUENCE_NODE:
        return yaml_sequence_to_obj(document, node, arena, obj);
    }

    GGL_LOGE("Unexpected node type from libyaml.");
    return GGL_ERR_FAILURE;
}

GglError ggl_yaml_decode_destructive(
    GglBuffer buf, GglArena *arena, GglObject *obj
) {
    static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    GGL_MTX_SCOPE_GUARD(&mtx);

    GGL_LOGT(
        "%s received yaml content: %.*s", __func__, (int) buf.len, buf.data
    );

    static yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser)) {
        GGL_LOGE("Parser initialization failed.");
        return GGL_ERR_FATAL;
    }
    yaml_parser_set_input_string(&parser, buf.data, buf.len);
    static yaml_document_t document;
    if (!yaml_parser_load(&parser, &document)) {
        GGL_LOGE(
            "Yaml parser load failed. Parser error: %s, at line %zu, column "
            "%zu",
            parser.problem,
            parser.problem_mark.line + 1,
            parser.problem_mark.column + 1
        );
        yaml_parser_delete(&parser);
        return GGL_ERR_PARSE;
    }
    yaml_node_t *root_node = yaml_document_get_root_node(&document);
    if (root_node == NULL) {
        GGL_LOGE("Yaml document is empty.");
        return GGL_ERR_NOENTRY;
    }

    // Handle NULL arena arg
    GglArena empty_arena = { 0 };
    GglArena *result_arena = (arena == NULL) ? &empty_arena : arena;

    // Copy to avoid committing allocation on error path
    GglArena arena_copy = *result_arena;

    GglError ret = yaml_to_obj(&document, root_node, &arena_copy, obj);

    if (obj != NULL) {
        if (ret == GGL_ERR_OK) {
            // Copy buffers (dynamically allocated by libyaml) into buf to mimic
            // in-place buffer decoding
            GglArena buf_arena = ggl_arena_init(buf);
            ret = ggl_arena_claim_obj_bufs(obj, &buf_arena);
        }

        if (ret == GGL_ERR_OK) {
            // Commit allocations
            *result_arena = arena_copy;
        }
    }

    yaml_document_delete(&document);
    yaml_parser_delete(&parser);

    return ret;
}
