// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggconfigd.h"
#include "helpers.h"
#include <ggl/core_bus/server.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <sqlite3.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static bool config_initialized = false;
static sqlite3 *config_database;
static const char *config_database_name = "config.db";

/// create the database to the correct schema
static GglError create_database(void) {
    GGL_LOGI("ggconfig_open", "creating the database");
    // create the initial table
    int result;
    char *err_message = 0;

    const char *create_query
        = "CREATE TABLE keyTable('keyid' INTEGER PRIMARY KEY "
          "AUTOINCREMENT unique not null,"
          "'keyvalue' TEXT NOT NULL COLLATE NOCASE  );"
          "CREATE TABLE relationTable( 'keyid' INT UNIQUE NOT NULL, "
          "'parentid' INT NOT NULL,"
          "primary key ( keyid ),"
          "foreign key ( keyid ) references keyTable(keyid),"
          "foreign key ( parentid ) references keyTable(keyid));"
          "CREATE TABLE valueTable( 'keyid' INT UNIQUE NOT NULL,"
          "'value' TEXT NOT NULL,"
          "'timeStamp' TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
          "foreign key(keyid) references keyTable(keyid) );"
          "CREATE TABLE version('version' TEXT DEFAULT '0.1');"
          "INSERT INTO version(version) VALUES (0.1);";

    result
        = sqlite3_exec(config_database, create_query, NULL, NULL, &err_message);
    if (result) {
        if (err_message) {
            GGL_LOGI("GGCONFIG", "%d %s", result, err_message);
            sqlite3_free(err_message);
        }
        return GGL_ERR_FAILURE;
    }
    return GGL_ERR_OK;
}

GglError ggconfig_open(void) {
    GglError return_value = GGL_ERR_FAILURE;
    if (config_initialized == false) {
        char *err_message = 0;
        // do configuration
        int rc = sqlite3_open(config_database_name, &config_database);
        if (rc) {
            GGL_LOGE(
                "ggconfiglib",
                "Cannot open the configuration database: %s",
                sqlite3_errmsg(config_database)
            );
            return_value = GGL_ERR_FAILURE;
        } else {
            GGL_LOGI("GGCONFIG", "Config database Opened");

            sqlite3_stmt *stmt;

            sqlite3_prepare_v2(
                config_database,
                "SELECT name FROM sqlite_master WHERE type = 'table' AND name "
                "='keyTable';",
                -1,
                &stmt,
                NULL
            );
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                GGL_LOGI("ggconfig_open", "found keyTable");
                return_value = GGL_ERR_OK;
            } else {
                return_value = create_database();
            }
        }
        // create a temporary table for subscriber data
        rc = sqlite3_exec(
            config_database,
            "CREATE TEMPORARY TABLE subscriberTable("
            "'keyid' INT NOT NULL,"
            "'handle' INT, "
            "FOREIGN KEY (keyid) REFERENCES "
            "keyTable(keyid))",
            NULL,
            NULL,
            &err_message
        );
        if (rc) {
            GGL_LOGE(
                "ggconfig_open",
                "failed to create temporary table %s",
                err_message
            );
            return_value = GGL_ERR_FAILURE;
        }
        config_initialized = true;
    } else {
        return_value = GGL_ERR_OK;
    }
    return return_value;
}

GglError ggconfig_close(void) {
    sqlite3_close(config_database);
    config_initialized = false;
    return GGL_ERR_OK;
}

static int64_t key_insert(GglBuffer *key) {
    sqlite3_stmt *key_insert_stmt;
    int64_t id = 0;
    GGL_LOGI("key_insert", "insert %.*s", (int) key->len, (char *) key->data);
    sqlite3_prepare_v2(
        config_database,
        "INSERT INTO keyTable(keyvalue) VALUES (?);",
        -1,
        &key_insert_stmt,
        NULL
    );
    // insert this element in the root level (as a key not in the relation )
    sqlite3_bind_text(
        key_insert_stmt, 1, (char *) key->data, (int) key->len, SQLITE_STATIC
    );
    sqlite3_step(key_insert_stmt);
    id = sqlite3_last_insert_rowid(config_database);
    GGL_LOGI(
        "key_insert", "insert %.*s result: %ld", (int) key->len, key->data, id
    );
    sqlite3_finalize(key_insert_stmt);
    return id;
}

static bool value_is_present_for_key(int64_t key_id) {
    sqlite3_stmt *find_value_stmt;
    bool return_value = false;
    GGL_LOGI("value_is_present_for_key", "checking id %ld", key_id);

    sqlite3_prepare_v2(
        config_database,
        "SELECT keyid FROM valueTable where keyid = ?;",
        -1,
        &find_value_stmt,
        NULL
    );
    sqlite3_bind_int64(find_value_stmt, 1, key_id);
    int rc = sqlite3_step(find_value_stmt);
    if (rc == SQLITE_ROW) {
        int64_t pid = sqlite3_column_int(find_value_stmt, 0);
        if (pid) {
            GGL_LOGI("value_is_present_for_key", "id %ld is present", key_id);
            return_value = true;
        }
    }
    return return_value;
}

static int64_t find_key_with_parent(GglBuffer *key, int64_t parent_key_id) {
    sqlite3_stmt *find_element_stmt;
    int64_t id = 0;
    GGL_LOGI(
        "find_key_with_parent",
        "searching for key %.*s with parent id %ld",
        (int) key->len,
        (char *) key->data,
        parent_key_id
    );
    sqlite3_prepare_v2(
        config_database,
        "SELECT kt.keyid "
        "FROM keyTable kt "
        "LEFT JOIN relationTable rt "
        "WHERE kt.keyid = rt.keyid AND "
        "kt.keyvalue = ? AND rt.parentid = ?;",
        -1,
        &find_element_stmt,
        NULL
    );
    sqlite3_bind_text(
        find_element_stmt, 1, (char *) key->data, (int) key->len, SQLITE_STATIC
    );
    sqlite3_bind_int64(find_element_stmt, 2, parent_key_id);
    int rc = sqlite3_step(find_element_stmt);
    GGL_LOGI("find_key_with_parent", "find element returned %d", rc);
    if (rc == SQLITE_ROW) {
        id = sqlite3_column_int(find_element_stmt, 0);
        GGL_LOGI(
            "find_key_with_parent",
            "found key %.*s with parent id %ld at %ld",
            (int) key->len,
            (char *) key->data,
            parent_key_id,
            id
        );
    } else {
        GGL_LOGI(
            "find_key_with_parent",
            "key %.*s with parent id %ld not found",
            (int) key->len,
            (char *) key->data,
            parent_key_id
        );
    }
    sqlite3_finalize(find_element_stmt);
    return id;
}

static int64_t get_or_create_key_at_root(GglBuffer *key) {
    sqlite3_stmt *root_check_stmt;
    int64_t id = 0;
    int rc = 0;
    GGL_LOGI(
        "get_or_create_key_at_root",
        "Found %.*s",
        (int) key->len,
        (char *) key->data
    );
    // get a keyid where the key is a root (first element of a path)
    sqlite3_prepare_v2(
        config_database,
        "SELECT keyid FROM keyTable WHERE keyid NOT IN (SELECT "
        "keyid FROM relationTable) AND keyvalue = ?;",
        -1,
        &root_check_stmt,
        NULL
    );

    sqlite3_bind_text(
        root_check_stmt, 1, (char *) key->data, (int) key->len, SQLITE_STATIC
    );
    rc = sqlite3_step(root_check_stmt);
    if (rc == SQLITE_ROW) { // exists as a root and here is the id
        id = sqlite3_column_int(root_check_stmt, 0);
        GGL_LOGI(
            "get_or_create_key_at_root",
            "Found %.*s at %ld",
            (int) key->len,
            (char *) key->data,
            id
        );
    } else {
        id = key_insert(key);
    }
    sqlite3_finalize(root_check_stmt);
    return id;
}

static void relation_insert(int64_t id, int64_t parent) {
    sqlite3_stmt *relation_insert_stmt;
    sqlite3_prepare_v2(
        config_database,
        "INSERT INTO relationTable(keyid,parentid) VALUES (?,?);",
        -1,
        &relation_insert_stmt,
        NULL
    );
    sqlite3_bind_int64(relation_insert_stmt, 1, id);
    sqlite3_bind_int64(relation_insert_stmt, 2, parent);
    int rc = sqlite3_step(relation_insert_stmt);
    if (rc == SQLITE_DONE || rc == SQLITE_OK) {
        GGL_LOGI(
            "relation_insert",
            "relation insert successful key:%ld, "
            "parent:%ld",
            id,
            parent
        );
    } else {
        GGL_LOGE(
            "relation_insert",
            "relation insert fail: %s",
            sqlite3_errmsg(config_database)
        );
    }
    sqlite3_finalize(relation_insert_stmt);
}

// TODO: add timestamp to the insert
static GglError value_insert(int64_t key_id, GglBuffer *value) {
    sqlite3_stmt *value_insert_stmt;
    GglError return_value = GGL_ERR_FAILURE;
    sqlite3_prepare_v2(
        config_database,
        "INSERT INTO valueTable(keyid,value) VALUES (?,?);",
        -1,
        &value_insert_stmt,
        NULL
    );
    sqlite3_bind_int64(value_insert_stmt, 1, key_id);
    sqlite3_bind_text(
        value_insert_stmt,
        2,
        (char *) value->data,
        (int) value->len,
        SQLITE_STATIC
    );
    int rc = sqlite3_step(value_insert_stmt);
    if (rc == SQLITE_DONE || rc == SQLITE_OK) {
        GGL_LOGI("ggconfig_insert", "value insert successful");
        return_value = GGL_ERR_OK;
    } else {
        GGL_LOGE(
            "ggconfig_insert",
            "value insert fail : %s",
            sqlite3_errmsg(config_database)
        );
    }
    sqlite3_finalize(value_insert_stmt);
    return return_value;
}

static GglError value_update(int64_t key_id, GglBuffer *value) {
    sqlite3_stmt *update_value_stmt;
    GglError return_value = GGL_ERR_FAILURE;

    sqlite3_prepare_v2(
        config_database,
        "UPDATE valueTable SET value = ? WHERE keyid = ?;",
        -1,
        &update_value_stmt,
        NULL
    );
    sqlite3_bind_text(
        update_value_stmt,
        1,
        (char *) value->data,
        (int) value->len,
        SQLITE_STATIC
    );
    sqlite3_bind_int64(update_value_stmt, 2, key_id);
    int rc = sqlite3_step(update_value_stmt);
    GGL_LOGI("ggconfig_insert", "%d", rc);
    if (rc == SQLITE_DONE || rc == SQLITE_OK) {
        GGL_LOGI("ggconfig_insert", "value update successful");
        return_value = GGL_ERR_OK;
    } else {
        GGL_LOGE(
            "ggconfig_insert",
            "value update fail : %s",
            sqlite3_errmsg(config_database)
        );
    }
    sqlite3_finalize(update_value_stmt);
    return return_value;
}

static int64_t get_key_id(GglList *key_path) {
    sqlite3_stmt *find_element_stmt;
    int64_t id = 0;
    GGL_LOGI("get_key_id", "searching for %s", print_key_path(key_path));

    sqlite3_prepare_v2(
        config_database,
        "WITH RECURSIVE path_cte(current_key_id, depth) AS ( "
        "    SELECT keyid, 1 "
        "    FROM keyTable "
        "    WHERE keyid NOT IN (SELECT keyid FROM relationTable) "
        "        AND keyvalue = ? "
        "    "
        "    UNION ALL "
        "    "
        "    SELECT kt.keyid, pc.depth + 1 "
        "    FROM path_cte pc "
        "    JOIN relationTable rt ON pc.current_key_id = rt.parentid "
        "    JOIN keyTable kt ON rt.keyid = kt.keyid "
        "    WHERE kt.keyvalue = ( "
        "        CASE pc.depth "
        "            WHEN 1 THEN ? "
        "            WHEN 2 THEN ? "
        "            WHEN 3 THEN ? "
        "            WHEN 4 THEN ? "
        "            WHEN 5 THEN ? "
        "            WHEN 6 THEN ? "
        "            WHEN 7 THEN ? "
        "            WHEN 8 THEN ? "
        "            WHEN 9 THEN ? "
        "            WHEN 10 THEN ? "
        "            WHEN 11 THEN ? "
        "            WHEN 12 THEN ? "
        "            WHEN 13 THEN ? "
        "            WHEN 14 THEN ? "
        "            WHEN 15 THEN ? "
        "            WHEN 16 THEN ? "
        "            WHEN 17 THEN ? "
        "            WHEN 18 THEN ? "
        "            WHEN 19 THEN ? "
        "            WHEN 20 THEN ? "
        "            WHEN 21 THEN ? "
        "            WHEN 22 THEN ? "
        "            WHEN 23 THEN ? "
        "            WHEN 24 THEN ? "
        "        END "
        "    ) "
        "    AND pc.depth < ? "
        ") "
        "SELECT current_key_id AS final_key_id "
        "FROM path_cte "
        "LIMIT 1 offset (? - 1); ",
        -1,
        &find_element_stmt,
        NULL
    );

    for (size_t index = 0; index < key_path->len; index++) {
        GglBuffer *key = (GglBuffer *) &key_path->items[index].buf;
        sqlite3_bind_text(
            find_element_stmt,
            (int) index + 1,
            (char *) key->data,
            (int) key->len,
            SQLITE_STATIC
        );
    }

    for (size_t index = key_path->len; index <= 24; index++) {
        sqlite3_bind_null(find_element_stmt, (int) index + 1);
    }

    sqlite3_bind_int(find_element_stmt, 26, (int) key_path->len);
    sqlite3_bind_int(find_element_stmt, 27, (int) key_path->len);

    int rc = sqlite3_step(find_element_stmt);
    GGL_LOGI("get_key_id", "find element returned %d", rc);
    if (rc == SQLITE_ROW) {
        id = sqlite3_column_int(find_element_stmt, 0);
        GGL_LOGI(
            "get_key_id", "found id for %s: %ld", print_key_path(key_path), id
        );
    } else {
        GGL_LOGI("get_key_id", "id not found for %s", print_key_path(key_path));
    }
    sqlite3_finalize(find_element_stmt);
    return id;
}

static int64_t create_key_path(GglList *key_path) {
    GglBuffer root_key_buffer = key_path->items[0].buf;
    int64_t parent_key_id = get_or_create_key_at_root(&root_key_buffer);
    int64_t current_key_id = parent_key_id;
    for (size_t index = 1; index < key_path->len; index++) {
        GglBuffer current_key_buffer = key_path->items[index].buf;
        current_key_id
            = find_key_with_parent(&current_key_buffer, parent_key_id);
        if (current_key_id == 0) { // not found
            current_key_id = key_insert(&current_key_buffer);
            relation_insert(current_key_id, parent_key_id);
        }
        parent_key_id = current_key_id;
    }
    return current_key_id;
}

GglError ggconfig_write_value_at_key(GglList *key_path, GglBuffer *value) {
    GglError return_value = GGL_ERR_FAILURE;
    if (config_initialized == false) {
        return GGL_ERR_FAILURE;
    }

    sqlite3_exec(config_database, "BEGIN TRANSACTION", NULL, NULL, NULL);

    int64_t id = get_key_id(key_path);
    if (id == 0) {
        id = create_key_path(key_path);
    }

    GGL_LOGI(
        "ggconfig_write_value_at_key",
        "time to insert/update %s",
        print_key_path(key_path)
    );
    if (value_is_present_for_key(id)) {
        return_value = value_update(id, value);
    } else {
        return_value = value_insert(id, value);
    }

    sqlite3_exec(config_database, "END TRANSACTION", NULL, NULL, NULL);

    // notify any subscribers for this key

    // TODO: read this comment copied from the JAVA and ensure this implements a
    // similar functionality A subscriber is told what Topic changed, but must
    // look in the Topic to get the new value.  There is no "old value"
    // provided, although the publish framework endeavors to suppress notifying
    // when the new value is the same as the old value. Subscribers do not
    // necessarily get notified on every change.  If a sequence of changes
    // happen in rapid succession, they may be collapsed into one notification.
    // This usually happens when a compound change occurs.

    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(
        config_database,
        "SELECT handle FROM subscriberTable S "
        "LEFT JOIN keyTable K "
        "WHERE S.keyid = K.keyid AND K.keyid = ?;",
        -1,
        &stmt,
        NULL
    );
    sqlite3_bind_int64(stmt, 1, id);
    int rc = 0;
    GGL_LOGI(
        "ggconfig_write_value_at_key",
        "subscription loop for %s at id %ld",
        print_key_path(key_path),
        id
    );
    do {
        rc = sqlite3_step(stmt);
        switch (rc) {
        case SQLITE_DONE:
            GGL_LOGI("subscription", "DONE");
            break;
        case SQLITE_ROW: {
            uint32_t handle = (uint32_t) sqlite3_column_int64(stmt, 0);
            GGL_LOGI("subscription", "Sending to %u", handle);
            ggl_respond(handle, GGL_OBJ(*value));
        } break;
        default:
            GGL_LOGI("subscription", "RC %d", rc);
            break;
        }
    } while (rc == SQLITE_ROW);

    sqlite3_finalize(stmt);

    GGL_LOGI(
        "ggconfig_write_value_at_key",
        "finished with %s",
        print_key_path(key_path)
    );

    return return_value;
}

GglError ggconfig_get_value_from_key(
    GglList *key_path, GglBuffer *value_buffer
) {
    sqlite3_stmt *stmt;
    GglError return_value = GGL_ERR_FAILURE;

    if (config_initialized == false) {
        return GGL_ERR_FAILURE;
    }

    sqlite3_exec(config_database, "BEGIN TRANSACTION", NULL, NULL, NULL);
    int64_t key_id = get_key_id(key_path);
    sqlite3_prepare_v2(
        config_database,
        "SELECT value FROM valueTable "
        "WHERE keyid = ?;",
        -1,
        &stmt,
        NULL
    );
    sqlite3_bind_int64(stmt, 1, key_id);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        const unsigned char *value_string = sqlite3_column_text(stmt, 0);
        unsigned long value_length
            = (unsigned long) sqlite3_column_bytes(stmt, 0);
        static unsigned char string_buffer[GGCONFIGD_MAX_VALUE_SIZE];
        value_buffer->data = string_buffer;
        memcpy(string_buffer, value_string, value_length);
        value_buffer->len = value_length;

        GGL_LOGI(
            "ggconfig_get",
            "%.*s",
            (int) value_buffer->len,
            (char *)
                value_buffer->data // TODO: is there risk of logging sensitive
                                   // values stored in config here? Or does
                                   // config not store sensitive values?
        );
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            GGL_LOGE("ggconfig_get", "%s", sqlite3_errmsg(config_database));
            return_value = GGL_ERR_FAILURE;
            value_buffer->data = NULL;
            value_buffer->len = 0;
        } else {
            return_value = GGL_ERR_OK;
        }
    }
    sqlite3_exec(config_database, "END TRANSACTION", NULL, NULL, NULL);
    sqlite3_finalize(stmt);
    return return_value;
}

GglError ggconfig_get_key_notification(GglList *key_path, uint32_t handle) {
    int64_t key_id;
    sqlite3_stmt *stmt;
    GglError return_value = GGL_ERR_FAILURE;

    if (config_initialized == false) {
        return GGL_ERR_FAILURE;
    }

    sqlite3_exec(config_database, "BEGIN TRANSACTION", NULL, NULL, NULL);
    // ensure this key is present in the key path. Key does not require a
    // value
    key_id = get_key_id(key_path);
    if (key_id == 0) {
        key_id = create_key_path(key_path);
    }
    GGL_LOGD(
        "get_key_notification",
        "Subscribing %d:%d to %s",
        handle && 0xFFFF0000 >> 16,
        handle && 0x0000FFFF,
        print_key_path(key_path)
    );
    // insert the key & handle data into the subscriber database
    GGL_LOGI("get_key_notification", "INSERT %ld, %d", key_id, handle);
    sqlite3_prepare_v2(
        config_database,
        "INSERT INTO subscriberTable(keyid, handle) VALUES (?,?);",
        -1,
        &stmt,
        NULL
    );
    sqlite3_bind_int64(stmt, 1, key_id);
    sqlite3_bind_int64(stmt, 2, handle);
    int rc = sqlite3_step(stmt);
    sqlite3_exec(config_database, "END TRANSACTION", NULL, NULL, NULL);
    if (SQLITE_DONE != rc) {
        GGL_LOGE(
            "get_key_notification", "%d %s", rc, sqlite3_errmsg(config_database)
        );
    } else {
        GGL_LOGT("get_key_notification", "Success");
        return_value = GGL_ERR_OK;
    }
    sqlite3_finalize(stmt);

    return return_value;
}
