// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGCONFIGD_EMBEDS_LIST_H
#define GGCONFIGD_EMBEDS_LIST_H

#define EMBED_FILE_LIST \
    EMBED_FILE(sql/create_db.sql, GGL_SQL_CREATE_DB) \
    EMBED_FILE(sql/create_sub_table.sql, GGL_SQL_CREATE_SUB_TABLE) \
    EMBED_FILE(sql/key_insert.sql, GGL_SQL_KEY_INSERT) \
    EMBED_FILE(sql/check_initialized.sql, GGL_SQL_CHECK_INITALIZED) \
    EMBED_FILE(sql/value_present.sql, GGL_SQL_VALUE_PRESENT) \
    EMBED_FILE(sql/get_key_with_parent.sql, GGL_SQL_GET_KEY_WITH_PARENT) \
    EMBED_FILE(sql/get_root_key.sql, GGL_SQL_GET_ROOT_KEY) \
    EMBED_FILE(sql/insert_relation.sql, GGL_SQL_INSERT_RELATION) \
    EMBED_FILE(sql/value_insert.sql, GGL_SQL_VALUE_INSERT) \
    EMBED_FILE(sql/value_update.sql, GGL_SQL_VALUE_UPDATE) \
    EMBED_FILE(sql/get_timestamp.sql, GGL_SQL_GET_TIMESTAMP) \
    EMBED_FILE(sql/find_element.sql, GGL_SQL_FIND_ELEMENT) \
    EMBED_FILE(sql/has_child.sql, GGL_SQL_HAS_CHILD) \
    EMBED_FILE(sql/get_subscribers.sql, GGL_SQL_GET_SUBSCRIBERS) \
    EMBED_FILE(sql/read_value.sql, GGL_SQL_READ_VALUE) \
    EMBED_FILE(sql/get_children.sql, GGL_SQL_GET_CHILDREN) \
    EMBED_FILE(sql/add_subscription.sql, GGL_SQL_ADD_SUBSCRIPTION)

#endif
