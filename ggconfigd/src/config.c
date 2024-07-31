// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggconfig.h"
#include <ctype.h>
#include <ggl/core_bus/server.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <sqlite3.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
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
        = "CREATE TABLE pathTable('pathid' INTEGER PRIMARY KEY "
          "AUTOINCREMENT unique not null,"
          "'pathvalue' TEXT NOT NULL UNIQUE COLLATE NOCASE  );"
          "CREATE TABLE relationTable( 'pathid' INT UNIQUE NOT NULL, "
          "'parentid' INT NOT NULL,"
          "primary key ( pathid ),"
          "foreign key ( pathid ) references pathTable(pathid),"
          "foreign key( parentid) references pathTable(pathid));"
          "CREATE TABLE valueTable( 'pathid' INT UNIQUE NOT NULL,"
          "'value' TEXT NOT NULL,"
          "'timeStamp' TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
          "foreign key(pathid) references pathTable(pathid) );"
          "CREATE TABLE version('version' TEXT DEFAULT '0.1');"
          "INSERT INTO version(version) VALUES (0.1);"
          "CREATE TRIGGER update_Timestamp_Trigger"
          "AFTER UPDATE On valueTable BEGIN "
          "UPDATE valueTable SET timeStamp = CURRENT_TIMESTAMP WHERE "
          "pathid = NEW.pathid;"
          "END;";

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
                "='pathTable';",
                -1,
                &stmt,
                NULL
            );
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                GGL_LOGI("ggconfig_open", "found pathtable");
                return_value = GGL_ERR_OK;
            } else {
                return_value = create_database();
            }
        }
        // create a temporary table for subscriber data
        rc = sqlite3_exec(
            config_database,
            "CREATE TEMPORARY TABLE subscriberTable("
            "'pathid' INT UNIQUE NOT NULL,"
            "'handle' INT, "
            "FOREIGN KEY (pathid) REFERENCES "
            "pathTable(pathid))",
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

static long long path_insert(GglBuffer *key) {
    sqlite3_stmt *path_insert_stmt;
    long long id = 0;
    GGL_LOGI("path_insert", "insert %.*s", (int) key->len, (char *) key->data);
    sqlite3_prepare_v2(
        config_database,
        "INSERT INTO pathTable(pathvalue) VALUES (?);",
        -1,
        &path_insert_stmt,
        NULL
    );
    // insert this element in the root level (as a path not in the relation )
    sqlite3_bind_text(
        path_insert_stmt, 1, (char *) key->data, (int) key->len, SQLITE_STATIC
    );
    sqlite3_step(path_insert_stmt);
    id = sqlite3_last_insert_rowid(config_database);
    GGL_LOGI(
        "path_insert", "insert %.*s result: %lld", (int) key->len, key->data, id
    );
    sqlite3_finalize(path_insert_stmt);
    return id;
}

static bool value_is_present_for_key(GglBuffer *key) {
    sqlite3_stmt *find_value_stmt;
    bool return_value = false;
    GGL_LOGI(
        "value_is_present_for_key",
        "checking %.*s",
        (int) key->len,
        (char *) key->data
    );
    sqlite3_prepare_v2(
        config_database,
        "SELECT pathid FROM valueTable where pathid = (SELECT pathid FROM "
        "pathTable WHERE pathValue = ?);",
        -1,
        &find_value_stmt,
        NULL
    );
    sqlite3_bind_text(
        find_value_stmt, 1, (char *) key->data, (int) key->len, SQLITE_STATIC
    );
    int rc = sqlite3_step(find_value_stmt);
    if (rc == SQLITE_ROW) {
        long long pid = sqlite3_column_int(find_value_stmt, 0);
        if (pid) {
            GGL_LOGI(
                "value_is_present_for_key",
                "%.*s is present",
                (int) key->len,
                (char *) key->data
            );
            return_value = true;
        }
    }
    return return_value;
}

static long long find_path_with_parent(GglBuffer *key) {
    sqlite3_stmt *find_element_stmt;
    long long id = 0;
    GGL_LOGI(
        "find_path_with_parent",
        "searching %.*s",
        (int) key->len,
        (char *) key->data
    );
    sqlite3_prepare_v2(
        config_database,
        "SELECT pathid FROM pathTable WHERE pathid IN (SELECT pathid FROM "
        "relationTable) AND pathvalue = ?;",
        -1,
        &find_element_stmt,
        NULL
    );
    // get the ID of this item after the parent
    sqlite3_bind_text(
        find_element_stmt, 1, (char *) key->data, (int) key->len, SQLITE_STATIC
    );
    int rc = sqlite3_step(find_element_stmt);
    GGL_LOGI("find_path_with_parent", "find element returned %d", rc);
    if (rc == SQLITE_ROW) {
        id = sqlite3_column_int(find_element_stmt, 0);
        GGL_LOGI(
            "find_path_with_parent",
            "found %.*s at %lld",
            (int) key->len,
            (char *) key->data,
            id
        );
    } else {
        GGL_LOGI(
            "find_path_with_parent",
            "%.*s not found",
            (int) key->len,
            (char *) key->data
        );
    }
    sqlite3_finalize(find_element_stmt);
    return id;
}

static long long get_parent_key_at_root(GglBuffer *key) {
    sqlite3_stmt *root_check_stmt;
    long long id = 0;
    int rc = 0;
    GGL_LOGI(
        "get_parent_key_at_root",
        "Found %.*s",
        (int) key->len,
        (char *) key->data
    );
    // get a pathid where the path is a root (first element of a path)
    sqlite3_prepare_v2(
        config_database,
        "SELECT pathid FROM pathTable WHERE pathid NOT IN (SELECT "
        "pathid FROM relationTable) AND pathvalue = ?;",
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
            "get_parent_key_at_root",
            "Found %.*s at %lld",
            (int) key->len,
            (char *) key->data,
            id
        );
    } else {
        id = path_insert(key);
    }
    sqlite3_finalize(root_check_stmt);
    return id;
}

static void relation_insert(long long id, long long parent) {
    sqlite3_stmt *relation_insert_stmt;
    sqlite3_prepare_v2(
        config_database,
        "INSERT INTO relationTable(pathid,parentid) VALUES (?,?);",
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
            "relation insert successful path:%lld, "
            "parent:%lld",
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

static GglError value_insert(GglBuffer *key, GglBuffer *value) {
    sqlite3_stmt *value_insert_stmt;
    GglError return_value = GGL_ERR_FAILURE;
    sqlite3_prepare_v2(
        config_database,
        "INSERT INTO valueTable(pathid,value) VALUES ( (SELECT pathid FROM "
        "pathTable where pathvalue = ?),?);",
        -1,
        &value_insert_stmt,
        NULL
    );
    sqlite3_bind_text(
        value_insert_stmt, 1, (char *) key->data, (int) key->len, SQLITE_STATIC
    );
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

static GglError value_update(GglBuffer *key, GglBuffer *value) {
    sqlite3_stmt *update_value_stmt;
    GglError return_value = GGL_ERR_FAILURE;

    sqlite3_prepare_v2(
        config_database,
        "UPDATE valueTable SET value = ? WHERE pathid = (SELECT pathid "
        "from pathTable where pathvalue = ?);",
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
    sqlite3_bind_text(
        update_value_stmt, 2, (char *) key->data, (int) key->len, SQLITE_STATIC
    );
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

static bool validate_key(GglBuffer *key) {
    // Verify that the path is alpha characters or / and nothing else
    if (!isalpha(key->data[0])) { // make sure the path starts with a character
        return false;
    }
    for (size_t x = 0; x < key->len; x++) {
        if (!isalpha(key->data[x]) && key->data[x] != '/') {
            return false;
        }
    }
    return true;
}

static long long get_path_id(GglBuffer *key) {
    sqlite3_stmt *find_element_stmt;
    long long id = 0;
    GGL_LOGI(
        "get_path_id", "searching %.*s", (int) key->len, (char *) key->data
    );
    sqlite3_prepare_v2(
        config_database,
        "SELECT pathid FROM pathTable WHERE pathvalue = ?;",
        -1,
        &find_element_stmt,
        NULL
    );
    // get the ID of this item after the parent
    sqlite3_bind_text(
        find_element_stmt, 1, (char *) key->data, (int) key->len, SQLITE_STATIC
    );
    int rc = sqlite3_step(find_element_stmt);
    GGL_LOGI("find_path_with_parent", "find element returned %d", rc);
    if (rc == SQLITE_ROW) {
        id = sqlite3_column_int(find_element_stmt, 0);
        GGL_LOGI(
            "get_path_id",
            "found %.*s at %lld",
            (int) key->len,
            (char *) key->data,
            id
        );
    } else {
        GGL_LOGI(
            "find_path_with_parent",
            "%.*s not found",
            (int) key->len,
            (char *) key->data
        );
    }
    sqlite3_finalize(find_element_stmt);
    return id;
}

static long long create_key_path(GglBuffer *key) {
    long long id = 0;
    long long parent_id = 0;
    int depth_count = 0;
    {
        GglBuffer parent_key_buffer;
        parent_key_buffer.data = key->data;
        parent_key_buffer.len = 0;

        sqlite3_exec(config_database, "BEGIN TRANSACTION", NULL, NULL, NULL);
        for (size_t index = 0; index < key->len; index++) {
            parent_key_buffer.len++;
            if (key->data[index + 1] == '/') {
                if (depth_count == 0) { // root level of the key path
                    id = get_parent_key_at_root(&parent_key_buffer);
                    if (id == 0) {
                        id = path_insert(&parent_key_buffer);
                    }
                } else { // all other key path levels
                    id = find_path_with_parent(&parent_key_buffer);

                    // if this id is not in the path, add it.
                    if (id == 0) {
                        GGL_LOGI(
                            "ggconfig_write_value_at_key",
                            "inserting %.*s, %lld, %lld",
                            (int) parent_key_buffer.len,
                            (char *) parent_key_buffer.data,
                            id,
                            parent_id
                        );
                        id = path_insert(&parent_key_buffer);
                        if (parent_id) {
                            relation_insert(id, parent_id);
                        }
                    }
                }
            }
            parent_id = id;
            depth_count++;
        }
    }
    id = find_path_with_parent(key);
    if (id == 0) {
        id = path_insert(key);
        relation_insert(id, parent_id);
    }
    return id;
}

GglError ggconfig_write_value_at_key(GglBuffer *key, GglBuffer *value) {
    GglError return_value = GGL_ERR_FAILURE;
    if (config_initialized == false) {
        return GGL_ERR_FAILURE;
    }
    if (key == 0 || key->data == 0 || key->len == 0) {
        return GGL_ERR_FAILURE;
    }
    if (validate_key(key) == false) {
        return GGL_ERR_INVALID;
    }

    if (get_path_id(key) == 0) {
        create_key_path(key);
    }

    GGL_LOGI(
        "ggconfig_insert",
        "time to insert/update %.*s",
        (int) key->len,
        (char *) key->data
    );
    if (value_is_present_for_key(key)) {
        return_value = value_update(key, value);
    } else {
        return_value = value_insert(key, value);
    }

    sqlite3_exec(config_database, "END TRANSACTION", NULL, NULL, NULL);

    // notify any subscribers for this key
    // use a subscriber table.
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(
        config_database,
        "SELECT handle FROM subscription_table as S LEFT JOIN pathTable as P "
        "WHERE S.pathid = P.pathid AND P.pathvalue = ?;",
        -1,
        &stmt,
        NULL
    );
    sqlite3_bind_text(
        stmt, 1, (char *) key->data, (int) key->len, SQLITE_STATIC
    );
    int rc = 0;
    while (rc != SQLITE_DONE) {
        rc = sqlite3_step(stmt);
        if (SQLITE_DONE != rc) {
            long long handle = sqlite3_column_int64(stmt, 0);
            GGL_LOGD("subscription", "Sending to %lld", handle);
            ggl_respond((uint32_t) handle, GGL_OBJ(*value));
        }
    }
    sqlite3_finalize(stmt);

    GGL_LOGI(
        "ggconfig_insert",
        "finished with %.*s",
        (int) key->len,
        (char *) key->data
    );

    return return_value;
}

GglError ggconfig_get_value_from_key(GglBuffer *key, GglBuffer *value_buffer) {
    sqlite3_stmt *stmt;
    GglError return_value = GGL_ERR_FAILURE;

    if (config_initialized == false) {
        return GGL_ERR_FAILURE;
    }

    sqlite3_prepare_v2(
        config_database,
        "SELECT V.value FROM pathTable P LEFT JOIN valueTable V WHERE P.pathid "
        "= V.pathid AND P.pathvalue = ?;",
        -1,
        &stmt,
        NULL
    );
    sqlite3_bind_text(
        stmt, 1, (char *) key->data, (int) key->len, SQLITE_STATIC
    );
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        const unsigned char *value_string = sqlite3_column_text(stmt, 0);
        unsigned long value_length
            = (unsigned long) sqlite3_column_bytes(stmt, 0);
        unsigned char *string_buffer = malloc(value_length);
        value_buffer->data = string_buffer;
        memcpy(string_buffer, value_string, value_length);
        value_buffer->len = value_length;

        GGL_LOGI(
            "ggconfig_get",
            "%.*s",
            (int) value_buffer->len,
            (char *) value_buffer->data
        );
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            GGL_LOGE("ggconfig_get", "%s", sqlite3_errmsg(config_database));
            return_value = GGL_ERR_FAILURE;
            free(string_buffer);
            value_buffer->data = NULL;
            value_buffer->len = 0;
        } else {
            return_value = GGL_ERR_OK;
        }
    }
    sqlite3_finalize(stmt);
    return return_value;
}

GglError ggconfig_get_key_notification(GglBuffer *key, uint32_t handle) {
    long long key_id;
    sqlite3_stmt *stmt;

    if (config_initialized == false) {
        return GGL_ERR_FAILURE;
    }

    // ensure this key is present in the key path. Key does not require a value
    key_id = get_path_id(key);
    if (key_id == 0) {
        key_id = create_key_path(key);
    }
    GGL_LOGD(
        "get_key_notification",
        "Subscribing %d to %.*s",
        handle,
        (int) key->len,
        (char *) key->data
    );
    // insert the key & handle data into the subscriber database
    sqlite3_prepare_v2(
        config_database,
        "INSERT INTO subscriberTable(pathid, handle) VALUES (?,?);",
        -1,
        &stmt,
        NULL
    );
    sqlite3_bind_int64(stmt, 1, key_id);
    sqlite3_bind_int64(stmt, 2, handle);
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        GGL_LOGE("get_key_notification", "%s", sqlite3_errmsg(config_database));
    }
    sqlite3_finalize(stmt);

    return GGL_ERR_FAILURE;
}
