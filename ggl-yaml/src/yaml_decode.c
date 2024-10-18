// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/yaml_decode.h"
#include "pthread.h"
#include <sys/types.h>
#include <assert.h>
#include <ggl/alloc.h>
#include <ggl/bump_alloc.h>
#include <ggl/cleanup.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <string.h>
#include <yaml.h>
#include <stdint.h>

static GglError yaml_to_obj(
    yaml_document_t *document,
    yaml_node_t *node,
    GglAlloc *alloc,
    GglObject *obj
);

static GglError yaml_node_to_buf(
    yaml_node_t *node, GglAlloc *alloc, GglBuffer *buf
) {
    assert(node != NULL);
    assert(alloc != NULL);
    assert(buf != NULL);
    assert(node->type == YAML_SCALAR_NODE);

    uint8_t *value = node->data.scalar.value;
    size_t len = strlen((char *) value);

    *buf = (GglBuffer) { .data = value, .len = len };
    return GGL_ERR_OK;
}

static GglError yaml_scalar_to_obj(
    yaml_node_t *node, GglAlloc *alloc, GglObject *obj
) {
    assert(node != NULL);
    assert(alloc != NULL);
    assert(obj != NULL);
    assert(node->type == YAML_SCALAR_NODE);

    GglBuffer result;
    GglError ret = yaml_node_to_buf(node, alloc, &result);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    *obj = GGL_OBJ_BUF(result);
    return GGL_ERR_OK;
}

// NOLINTNEXTLINE(misc-no-recursion)
static GglError yaml_mapping_to_obj(
    yaml_document_t *document,
    yaml_node_t *node,
    GglAlloc *alloc,
    GglObject *obj
) {
    assert(document != NULL);
    assert(node != NULL);
    assert(alloc != NULL);
    assert(obj != NULL);
    assert(node->type == YAML_MAPPING_NODE);

    if (node->data.mapping.pairs.top < node->data.mapping.pairs.start) {
        GGL_LOGE("Unexpected result from libyaml.");
        return GGL_ERR_FAILURE;
    }

    size_t len = (size_t) (node->data.mapping.pairs.top
                           - node->data.mapping.pairs.start);

    GglKV *pairs = GGL_ALLOCN(alloc, GglKV, len);
    if (pairs == NULL) {
        GGL_LOGE("Insufficent memory to decode yaml.");
        return GGL_ERR_NOMEM;
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

        GglObject key_obj;
        GglError ret = yaml_to_obj(document, key_node, alloc, &key_obj);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
        assert(key_obj.type == GGL_TYPE_BUF);

        pairs[i].key = key_obj.buf;

        ret = yaml_to_obj(document, value_node, alloc, &pairs[i].val);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    *obj = GGL_OBJ_MAP((GglMap) { .pairs = pairs, .len = len });
    return GGL_ERR_OK;
}

// NOLINTNEXTLINE(misc-no-recursion)
static GglError yaml_sequence_to_obj(
    yaml_document_t *document,
    yaml_node_t *node,
    GglAlloc *alloc,
    GglObject *obj
) {
    assert(document != NULL);
    assert(node != NULL);
    assert(alloc != NULL);
    assert(obj != NULL);
    assert(node->type == YAML_SEQUENCE_NODE);

    if (node->data.sequence.items.top < node->data.sequence.items.start) {
        GGL_LOGE("Unexpected result from libyaml.");
        return GGL_ERR_FAILURE;
    }

    size_t len = (size_t) (node->data.sequence.items.top
                           - node->data.sequence.items.start);

    GglObject *items = GGL_ALLOCN(alloc, GglObject, len);
    if (items == NULL) {
        GGL_LOGE("Insufficent memory to decode yaml.");
        return GGL_ERR_NOMEM;
    }

    yaml_node_item_t *item_nodes = node->data.sequence.items.start;
    for (size_t i = 0; i < len; i++) {
        yaml_node_t *item_node
            = yaml_document_get_node(document, item_nodes[i]);
        if (item_node == NULL) {
            GGL_LOGE("Yaml sequence node NULL.");
            return GGL_ERR_FAILURE;
        }

        GglError ret = yaml_to_obj(document, item_node, alloc, &items[i]);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    *obj = GGL_OBJ_LIST((GglList) { .items = items, .len = len });
    return GGL_ERR_OK;
}

// NOLINTNEXTLINE(misc-no-recursion)
static GglError yaml_to_obj(
    yaml_document_t *document,
    yaml_node_t *node,
    GglAlloc *alloc,
    GglObject *obj
) {
    assert(document != NULL);
    assert(node != NULL);
    assert(alloc != NULL);
    assert(obj != NULL);

    switch (node->type) {
    case YAML_NO_NODE: {
        GGL_LOGE("Unexpected missing node from libyaml.");
        return GGL_ERR_FAILURE;
    }
    case YAML_SCALAR_NODE:
        return yaml_scalar_to_obj(node, alloc, obj);
    case YAML_MAPPING_NODE:
        return yaml_mapping_to_obj(document, node, alloc, obj);
    case YAML_SEQUENCE_NODE:
        return yaml_sequence_to_obj(document, node, alloc, obj);
    }

    GGL_LOGE("Unexpected node type from libyaml.");
    return GGL_ERR_FAILURE;
}

GglError ggl_yaml_decode_destructive(
    GglBuffer buf, GglAlloc *alloc, GglObject *obj
) {
    static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    GGL_MTX_SCOPE_GUARD(&mtx);

    static yaml_parser_t parser;
    yaml_parser_initialize(&parser);
    yaml_parser_set_input_string(&parser, buf.data, buf.len);
    static yaml_document_t document;
    yaml_parser_load(&parser, &document);
    yaml_node_t *root_node = yaml_document_get_root_node(&document);

    GglError ret = yaml_to_obj(&document, root_node, alloc, obj);

    if (ret == GGL_ERR_OK) {
        GglBumpAlloc balloc = ggl_bump_alloc_init(buf);
        ret = ggl_obj_buffer_copy(obj, &balloc.alloc);
    }

    yaml_document_delete(&document);
    yaml_parser_delete(&parser);

    return ret;
}
