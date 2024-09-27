// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggconfigd.h"
#include "ggl/alloc.h"
#include "helpers.h"
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/constants.h>
#include <ggl/core_bus/server.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <sqlite3.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

/// Enable defer for finalizing sql statements
GGL_DEFINE_DEFER(
    sqlite3_finalize, sqlite3_stmt *, stmt, sqlite3_finalize(*stmt)
)

static bool config_initialized = false;
static sqlite3 *config_database;
static const char *config_database_name = "config.db";

/// create the database to the correct schema
static GglError create_database(void) {
    GGL_LOGI("create_database", "creating the database");
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
          "'timeStamp' INTEGER NOT NULL,"
          "foreign key(keyid) references keyTable(keyid) );"
          "CREATE TABLE version('version' TEXT DEFAULT '0.1');"
          "INSERT INTO version(version) VALUES (0.1);";

    result
        = sqlite3_exec(config_database, create_query, NULL, NULL, &err_message);
    if (result != SQLITE_OK) {
        if (err_message) {
            GGL_LOGI("GGCONFIG", "%d %s", result, err_message);
            sqlite3_free(err_message);
        }
        return GGL_ERR_FAILURE;
    }
    return GGL_ERR_OK;
}

GglError ggconfig_open(void) {
    GglError return_err = GGL_ERR_FAILURE;
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
            return_err = GGL_ERR_FAILURE;
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
            GGL_DEFER(sqlite3_finalize, stmt);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                GGL_LOGI("ggconfig_open", "found keyTable");
                return_err = GGL_ERR_OK;
            } else {
                return_err = create_database();
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
            return_err = GGL_ERR_FAILURE;
        }
        config_initialized = true;
    } else {
        return_err = GGL_ERR_OK;
    }
    return return_err;
}

GglError ggconfig_close(void) {
    sqlite3_close(config_database);
    config_initialized = false;
    return GGL_ERR_OK;
}

static GglError key_insert(GglBuffer *key, int64_t *id_output) {
    GGL_LOGD("key_insert", "insert %.*s", (int) key->len, (char *) key->data);
    sqlite3_stmt *key_insert_stmt;
    sqlite3_prepare_v2(
        config_database,
        "INSERT INTO keyTable(keyvalue) VALUES (?);",
        -1,
        &key_insert_stmt,
        NULL
    );
    GGL_DEFER(sqlite3_finalize, key_insert_stmt);
    sqlite3_bind_text(
        key_insert_stmt, 1, (char *) key->data, (int) key->len, SQLITE_STATIC
    );
    if (sqlite3_step(key_insert_stmt) != SQLITE_DONE) {
        GGL_LOGE(
            "key_insert",
            "failed to insert key: %.*s with error: %s",
            (int) key->len,
            (char *) key->data,
            sqlite3_errmsg(config_database)
        );
        return GGL_ERR_FAILURE;
    }
    *id_output = sqlite3_last_insert_rowid(config_database);
    GGL_LOGD(
        "key_insert",
        "insert %.*s result: %ld",
        (int) key->len,
        key->data,
        *id_output
    );
    return GGL_ERR_OK;
}

static GglError value_is_present_for_key(
    int64_t key_id, bool *value_is_present_output
) {
    GGL_LOGD("value_is_present_for_key", "checking id %ld", key_id);

    sqlite3_stmt *find_value_stmt;
    sqlite3_prepare_v2(
        config_database,
        "SELECT keyid FROM valueTable where keyid = ?;",
        -1,
        &find_value_stmt,
        NULL
    );
    GGL_DEFER(sqlite3_finalize, find_value_stmt);
    sqlite3_bind_int64(find_value_stmt, 1, key_id);
    int rc = sqlite3_step(find_value_stmt);
    if (rc == SQLITE_ROW) {
        int64_t pid = sqlite3_column_int(find_value_stmt, 0);
        if (pid) {
            GGL_LOGD(
                "value_is_present_for_key", "id %ld does have a value", key_id
            );
            *value_is_present_output = true;
            return GGL_ERR_OK;
        }
        GGL_LOGE(
            "value_is_present_for_key",
            "checking presence of value for key id %ld failed",
            key_id
        );
        return GGL_ERR_FAILURE;
    }
    if (rc == SQLITE_DONE) {
        GGL_LOGD(
            "value_is_present_for_key", "id %ld does not have a value", key_id
        );
        *value_is_present_output = false;
        return GGL_ERR_OK;
    }
    GGL_LOGE(
        "value_is_present_for_key",
        "checking id %ld failed with error: %s",
        key_id,
        sqlite3_errmsg(config_database)
    );
    return GGL_ERR_FAILURE;
}

static GglError find_key_with_parent(
    GglBuffer *key, int64_t parent_key_id, int64_t *key_id_output
) {
    int64_t id = 0;
    GGL_LOGD(
        "find_key_with_parent",
        "searching for key %.*s with parent id %ld",
        (int) key->len,
        (char *) key->data,
        parent_key_id
    );
    sqlite3_stmt *find_element_stmt;
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
    GGL_DEFER(sqlite3_finalize, find_element_stmt);
    sqlite3_bind_text(
        find_element_stmt, 1, (char *) key->data, (int) key->len, SQLITE_STATIC
    );
    sqlite3_bind_int64(find_element_stmt, 2, parent_key_id);
    int rc = sqlite3_step(find_element_stmt);
    GGL_LOGD("find_key_with_parent", "find element returned %d", rc);
    if (rc == SQLITE_ROW) {
        id = sqlite3_column_int(find_element_stmt, 0);
        GGL_LOGD(
            "find_key_with_parent",
            "found key %.*s with parent id %ld at %ld",
            (int) key->len,
            (char *) key->data,
            parent_key_id,
            id
        );
        *key_id_output = id;
        return GGL_ERR_OK;
    }
    if (rc == SQLITE_DONE) {
        GGL_LOGI(
            "find_key_with_parent",
            "key %.*s with parent id %ld not found",
            (int) key->len,
            (char *) key->data,
            parent_key_id
        );
        return GGL_ERR_NOENTRY;
    }
    GGL_LOGE(
        "find_key_with_parent",
        "finding key %.*s with parent id %ld failed with error: %s",
        (int) key->len,
        (char *) key->data,
        parent_key_id,
        sqlite3_errmsg(config_database)
    );
    return GGL_ERR_FAILURE;
}

// get or create a keyid where the key is a root (first element of a path)
static GglError get_or_create_key_at_root(GglBuffer *key, int64_t *id_output) {
    GGL_LOGD(
        "get_or_create_key_at_root",
        "Checking %.*s",
        (int) key->len,
        (char *) key->data
    );
    int64_t id = 0;

    sqlite3_stmt *root_check_stmt;
    sqlite3_prepare_v2(
        config_database,
        "SELECT keyid FROM keyTable WHERE keyid NOT IN (SELECT "
        "keyid FROM relationTable) AND keyvalue = ?;",
        -1,
        &root_check_stmt,
        NULL
    );
    GGL_DEFER(sqlite3_finalize, root_check_stmt);
    sqlite3_bind_text(
        root_check_stmt, 1, (char *) key->data, (int) key->len, SQLITE_STATIC
    );
    int rc = sqlite3_step(root_check_stmt);
    if (rc == SQLITE_ROW) { // exists as a root and here is the id
        id = sqlite3_column_int(root_check_stmt, 0);
        GGL_LOGD(
            "get_or_create_key_at_root",
            "Found %.*s at %ld",
            (int) key->len,
            (char *) key->data,
            id
        );
    } else if (rc == SQLITE_DONE) { // doesn't exist at root, so we need to
                                    // create the key and get the id
        GglError err = key_insert(key, &id);
        if (err != GGL_ERR_OK) {
            return GGL_ERR_FAILURE;
        }
    } else {
        GGL_LOGE(
            "get_or_create_key_at_root",
            "finding key %.*s failed with error: %s",
            (int) key->len,
            (char *) key->data,
            sqlite3_errmsg(config_database)
        );
        return GGL_ERR_FAILURE;
    }
    *id_output = id;
    return GGL_ERR_OK;
}

static GglError relation_insert(int64_t id, int64_t parent) {
    sqlite3_stmt *relation_insert_stmt;
    sqlite3_prepare_v2(
        config_database,
        "INSERT INTO relationTable(keyid,parentid) VALUES (?,?);",
        -1,
        &relation_insert_stmt,
        NULL
    );
    GGL_DEFER(sqlite3_finalize, relation_insert_stmt);
    sqlite3_bind_int64(relation_insert_stmt, 1, id);
    sqlite3_bind_int64(relation_insert_stmt, 2, parent);
    int rc = sqlite3_step(relation_insert_stmt);
    if (rc == SQLITE_DONE || rc == SQLITE_OK) {
        GGL_LOGD(
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
        return GGL_ERR_FAILURE;
    }
    return GGL_ERR_OK;
}

static GglError value_insert(
    int64_t key_id, GglBuffer *value, int64_t timestamp
) {
    GglError return_err = GGL_ERR_FAILURE;
    sqlite3_stmt *value_insert_stmt;
    sqlite3_prepare_v2(
        config_database,
        "INSERT INTO valueTable(keyid,value,timeStamp) VALUES (?,?,?);",
        -1,
        &value_insert_stmt,
        NULL
    );
    GGL_DEFER(sqlite3_finalize, value_insert_stmt);
    sqlite3_bind_int64(value_insert_stmt, 1, key_id);
    sqlite3_bind_text(
        value_insert_stmt,
        2,
        (char *) value->data,
        (int) value->len,
        SQLITE_STATIC
    );
    sqlite3_bind_int64(value_insert_stmt, 3, timestamp);
    int rc = sqlite3_step(value_insert_stmt);
    if (rc == SQLITE_DONE || rc == SQLITE_OK) {
        GGL_LOGD("value_insert", "value insert successful");
        return_err = GGL_ERR_OK;
    } else {
        GGL_LOGE(
            "value_insert",
            "value insert fail with rc %d and error %s",
            rc,
            sqlite3_errmsg(config_database)
        );
        return_err = GGL_ERR_FAILURE;
    }
    return return_err;
}

static GglError value_update(
    int64_t key_id, GglBuffer *value, int64_t timestamp
) {
    GglError return_err = GGL_ERR_FAILURE;

    sqlite3_stmt *update_value_stmt;
    sqlite3_prepare_v2(
        config_database,
        "UPDATE valueTable SET value = ?, timeStamp = ? WHERE keyid = ?;",
        -1,
        &update_value_stmt,
        NULL
    );
    GGL_DEFER(sqlite3_finalize, update_value_stmt);
    sqlite3_bind_text(
        update_value_stmt,
        1,
        (char *) value->data,
        (int) value->len,
        SQLITE_STATIC
    );
    sqlite3_bind_int64(update_value_stmt, 2, timestamp);
    sqlite3_bind_int64(update_value_stmt, 3, key_id);
    int rc = sqlite3_step(update_value_stmt);
    if (rc == SQLITE_DONE || rc == SQLITE_OK) {
        GGL_LOGD("value_update", "value update successful");
        return_err = GGL_ERR_OK;
    } else {
        GGL_LOGE(
            "value_update",
            "value update fail with rc %d and error %s",
            rc,
            sqlite3_errmsg(config_database)
        );
        return_err = GGL_ERR_FAILURE;
    }
    return return_err;
}

static GglError value_get_timestamp(
    int64_t id, int64_t *existing_timestamp_output
) {
    sqlite3_stmt *get_timestamp_stmt;
    sqlite3_prepare_v2(
        config_database,
        "SELECT timeStamp FROM valueTable WHERE keyid = ?;",
        -1,
        &get_timestamp_stmt,
        NULL
    );
    GGL_DEFER(sqlite3_finalize, get_timestamp_stmt);
    sqlite3_bind_int64(get_timestamp_stmt, 1, id);
    int rc = sqlite3_step(get_timestamp_stmt);
    if (rc == SQLITE_ROW) {
        int64_t timestamp = sqlite3_column_int64(get_timestamp_stmt, 0);
        *existing_timestamp_output = timestamp;
        return GGL_ERR_OK;
    }
    if (rc == SQLITE_DONE) {
        return GGL_ERR_NOENTRY;
    }
    GGL_LOGE(
        "value_get_timestamp",
        "getting timestamp for id %ld failed with error: %s",
        id,
        sqlite3_errmsg(config_database)
    );
    return GGL_ERR_FAILURE;
}

// key_ids_output must point to an empty GglObjVec with capacity
// MAX_KEY_PATH_DEPTH
static GglError get_key_ids(GglList *key_path, GglObjVec *key_ids_output) {
    GGL_LOGD("get_key_ids", "searching for %s", print_key_path(key_path));

    sqlite3_stmt *find_element_stmt;
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
        "SELECT current_key_id AS key_id "
        "FROM path_cte ",
        -1,
        &find_element_stmt,
        NULL
    );
    GGL_DEFER(sqlite3_finalize, find_element_stmt);

    for (size_t index = 0; index < key_path->len; index++) {
        GglBuffer *key = &key_path->items[index].buf;
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

    for (size_t i = 0; i < key_path->len; i++) {
        int rc = sqlite3_step(find_element_stmt);
        if (rc == SQLITE_DONE) {
            GGL_LOGI(
                "get_key_ids",
                "id not found for key %d in %s",
                (int) i,
                print_key_path(key_path)
            );
            return GGL_ERR_NOENTRY;
        }
        if (rc != SQLITE_ROW) {
            GGL_LOGE(
                "get_key_ids",
                "get key id for key %d in %s fail: %s",
                (int) i,
                print_key_path(key_path),
                sqlite3_errmsg(config_database)
            );
            return GGL_ERR_FAILURE;
        }
        int64_t id = sqlite3_column_int(find_element_stmt, 0);
        GGL_LOGD(
            "get_key_ids",
            "found id for key %d in %s: %ld",
            (int) i,
            print_key_path(key_path),
            id
        );
        ggl_obj_vec_push(key_ids_output, GGL_OBJ_I64(id));
    }

    return GGL_ERR_OK;
}

// create_key_path assumes that the entire key_path does not already exist in
// the database (i.e. at least one key needs to be created). Behavior is
// undefined if the key_path fully exists already. Thus it should only be used
// within a transaction and after checking that the key_path does not fully
// exist.
// key_ids_output must point to an empty GglObjVec with capacity
// MAX_KEY_PATH_DEPTH
static GglError create_key_path(GglList *key_path, GglObjVec *key_ids_output) {
    GglBuffer root_key_buffer = key_path->items[0].buf;
    int64_t parent_key_id;
    GglError err = get_or_create_key_at_root(&root_key_buffer, &parent_key_id);
    if (err != GGL_ERR_OK) {
        return err;
    }
    ggl_obj_vec_push(key_ids_output, GGL_OBJ_I64(parent_key_id));
    bool value_is_present_for_root_key;
    err = value_is_present_for_key(
        parent_key_id, &value_is_present_for_root_key
    );
    if (err != GGL_ERR_OK) {
        GGL_LOGE(
            "create_key_path",
            "failed to check for value for root key %.*s with id %ld with "
            "error %d",
            (int) root_key_buffer.len,
            (char *) root_key_buffer.data,
            parent_key_id,
            (int) err
        );
        return err;
    }
    if (value_is_present_for_root_key) {
        GGL_LOGW(
            "create_key_path",
            "value already present for root key %.*s with id %ld",
            (int) root_key_buffer.len,
            (char *) root_key_buffer.data,
            parent_key_id
        );
        return GGL_ERR_FAILURE;
    }

    int64_t current_key_id = parent_key_id;
    for (size_t index = 1; index < key_path->len; index++) {
        GglBuffer current_key_buffer = key_path->items[index].buf;
        err = find_key_with_parent(
            &current_key_buffer, parent_key_id, &current_key_id
        );
        if (err == GGL_ERR_NOENTRY) {
            err = key_insert(&current_key_buffer, &current_key_id);
            if (err != GGL_ERR_OK) {
                return err;
            }
            err = relation_insert(current_key_id, parent_key_id);
            if (err != GGL_ERR_OK) {
                return err;
            }
        } else if (err == GGL_ERR_OK) { // the key exists and we got the id
            bool value_is_present;
            err = value_is_present_for_key(current_key_id, &value_is_present);
            if (err != GGL_ERR_OK) {
                GGL_LOGE(
                    "create_key_path",
                    "failed to check for value for key %.*s with id %ld with "
                    "error %d",
                    (int) current_key_buffer.len,
                    (char *) current_key_buffer.data,
                    current_key_id,
                    (int) err
                );
                return err;
            }
            if (value_is_present) {
                GGL_LOGW(
                    "create_key_path",
                    "value already present for key %.*s with id %ld",
                    (int) current_key_buffer.len,
                    (char *) current_key_buffer.data,
                    current_key_id
                );
                return GGL_ERR_FAILURE;
            }
        } else {
            return err;
        }
        ggl_obj_vec_push(key_ids_output, GGL_OBJ_I64(current_key_id));
        parent_key_id = current_key_id;
    }
    return GGL_ERR_OK;
}

static GglError child_is_present_for_key(
    int64_t key_id, bool *child_is_present_output
) {
    GglError return_err = GGL_ERR_FAILURE;

    sqlite3_stmt *child_check_stmt;
    sqlite3_prepare_v2(
        config_database,
        "SELECT 1 FROM relationTable WHERE parentid = ? LIMIT 1;",
        -1,
        &child_check_stmt,
        NULL
    );
    GGL_DEFER(sqlite3_finalize, child_check_stmt);
    sqlite3_bind_int64(child_check_stmt, 1, key_id);
    int rc = sqlite3_step(child_check_stmt);
    if (rc == SQLITE_ROW) {
        *child_is_present_output = true;
        return_err = GGL_ERR_OK;
    } else if (rc == SQLITE_DONE) {
        *child_is_present_output = false;
        return_err = GGL_ERR_OK;
    } else {
        GGL_LOGE(
            "child_is_present_for_key",
            "child check fail : %s",
            sqlite3_errmsg(config_database)
        );
        return_err = GGL_ERR_FAILURE;
    }
    return return_err;
}

static GglError notify_single_key(
    int64_t notify_key_id, GglList *changed_key_path
) {
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
    GGL_DEFER(sqlite3_finalize, stmt);
    sqlite3_bind_int64(stmt, 1, notify_key_id);
    int rc = 0;
    GGL_LOGD(
        "notify_single_key",
        "notifying subscribers on key with id %ld that key %s has changed",
        notify_key_id,
        print_key_path(changed_key_path)
    );
    do {
        rc = sqlite3_step(stmt);
        switch (rc) {
        case SQLITE_DONE:
            GGL_LOGD("notify_single_key", "DONE");
            break;
        case SQLITE_ROW: {
            uint32_t handle = (uint32_t) sqlite3_column_int64(stmt, 0);
            GGL_LOGD("notify_single_key", "Sending to %u", handle);
            ggl_respond(handle, GGL_OBJ(*changed_key_path));
        } break;
        default:
            GGL_LOGE(
                "notify_single_key",
                "Unexpected rc %d while getting handles to notify for key with "
                "id %ld with error: %s",
                rc,
                notify_key_id,
                sqlite3_errmsg(config_database)
            );
            return GGL_ERR_FAILURE;
            break;
        }
    } while (rc == SQLITE_ROW);

    return GGL_ERR_OK;
}

// Given a key path and the ids of the keys in that path, notify each key along
// the path that the value at the tip of the key path has changed
static GglError notify_nested_key(GglList *key_path, GglObjVec key_ids) {
    GglError return_err = GGL_ERR_OK;
    for (size_t i = 0; i < key_ids.list.len; i++) {
        GglError err = notify_single_key(key_ids.list.items[i].i64, key_path);
        if (err != GGL_ERR_OK) {
            return_err = GGL_ERR_FAILURE;
        }
    }
    return return_err;
}

GglError ggconfig_write_value_at_key(
    GglList *key_path, GglBuffer *value, int64_t timestamp
) {
    if (config_initialized == false) {
        return GGL_ERR_FAILURE;
    }

    GGL_LOGI(
        "ggconfig_write_value_at_key",
        "starting request to insert/update key: %s",
        print_key_path(key_path)
    );

    sqlite3_exec(config_database, "BEGIN TRANSACTION", NULL, NULL, NULL);

    GglObject ids_array[MAX_KEY_PATH_DEPTH];
    GglObjVec ids = { .list = { .items = ids_array, .len = 0 },
                      .capacity = MAX_KEY_PATH_DEPTH };
    int64_t last_key_id;
    GglError err = get_key_ids(key_path, &ids);
    if (err == GGL_ERR_NOENTRY) {
        ids.list.len = 0; // Reset the ids vector to be populated fresh
        err = create_key_path(key_path, &ids);
        if (err != GGL_ERR_OK) {
            sqlite3_exec(config_database, "ROLLBACK", NULL, NULL, NULL);
            return err;
        }

        last_key_id = ids.list.items[ids.list.len - 1].i64;
        value_insert(last_key_id, value, timestamp);
        sqlite3_exec(config_database, "END TRANSACTION", NULL, NULL, NULL);
        err = notify_nested_key(key_path, ids);
        if (err != GGL_ERR_OK) {
            GGL_LOGE(
                "ggconfig_write_value_at_key",
                "failed to notify all subscribers about update for key path %s "
                "with error %d",
                print_key_path(key_path),
                (int) err
            );
        }
        return GGL_ERR_OK;
    }
    if (err != GGL_ERR_OK) {
        GGL_LOGE(
            "ggconfig_write_value_at_key",
            "failed to get key id for key path %s with error %d",
            print_key_path(key_path),
            (int) err
        );
        sqlite3_exec(config_database, "ROLLBACK", NULL, NULL, NULL);
        return err;
    }
    last_key_id = ids.list.items[ids.list.len - 1].i64;
    bool child_is_present;
    err = child_is_present_for_key(last_key_id, &child_is_present);
    if (err != GGL_ERR_OK) {
        GGL_LOGE(
            "ggconfig_write_value_at_key",
            "failed to check for child presence for key %s with id %ld with "
            "error %d",
            print_key_path(key_path),
            last_key_id,
            (int) err
        );
        sqlite3_exec(config_database, "ROLLBACK", NULL, NULL, NULL);
        return err;
    }
    if (child_is_present) {
        GGL_LOGW(
            "ggconfig_write_value_at_key",
            "key %s with id %ld is a map with one or more children, so it can "
            "not also store a value",
            print_key_path(key_path),
            last_key_id
        );
        sqlite3_exec(config_database, "ROLLBACK", NULL, NULL, NULL);
        return GGL_ERR_FAILURE;
    }

    // we now know that the key already exists and does not have a child.
    // Therefore, it stores a value currently.

    int64_t existing_timestamp;
    err = value_get_timestamp(last_key_id, &existing_timestamp);
    if (err != GGL_ERR_OK) {
        GGL_LOGE(
            "ggconfig_write_value_at_key",
            "failed to get timestamp for key %s with id %ld with error %d",
            print_key_path(key_path),
            last_key_id,
            (int) err
        );
        sqlite3_exec(config_database, "ROLLBACK", NULL, NULL, NULL);
        return err;
    }
    if (existing_timestamp > timestamp) {
        GGL_LOGI(
            "ggconfig_write_value_at_key",
            "key %s has an existing timestamp %ld newer than provided "
            "timestamp %ld, so it will not be updated",
            print_key_path(key_path),
            existing_timestamp,
            timestamp
        );
        sqlite3_exec(config_database, "END TRANSACTION", NULL, NULL, NULL);
        return GGL_ERR_OK;
    }

    err = value_update(last_key_id, value, timestamp);
    if (err != GGL_ERR_OK) {
        GGL_LOGE(
            "ggconfig_write_value_at_key",
            "failed to update value for key %s with id %ld with error %d",
            print_key_path(key_path),
            last_key_id,
            (int) err
        );
        sqlite3_exec(config_database, "ROLLBACK", NULL, NULL, NULL);
        return err;
    }
    sqlite3_exec(config_database, "END TRANSACTION", NULL, NULL, NULL);

    err = notify_nested_key(key_path, ids);
    if (err != GGL_ERR_OK) {
        GGL_LOGE(
            "ggconfig_write_value_at_key",
            "failed to notify subscribers about update for key path %s with "
            "error %d",
            print_key_path(key_path),
            (int) err
        );
    }
    return GGL_ERR_OK;
}

static GglError read_value_at_key(
    int64_t key_id, GglObject *value, GglAlloc *alloc
) {
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(
        config_database,
        "SELECT value FROM valueTable WHERE keyid = ?;",
        -1,
        &stmt,
        NULL
    );
    GGL_DEFER(sqlite3_finalize, stmt);
    sqlite3_bind_int64(stmt, 1, key_id);
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        GGL_LOGI("read_value_at_key", "no value found for key id %ld", key_id);
        return GGL_ERR_NOENTRY;
    }
    if (rc != SQLITE_ROW) {
        GGL_LOGE(
            "read_value_at_key",
            "failed to read value for key id %ld with rc %d and error %s",
            key_id,
            rc,
            sqlite3_errmsg(config_database)
        );
        return GGL_ERR_FAILURE;
    }
    const uint8_t *value_string = sqlite3_column_text(stmt, 0);
    unsigned long value_length = (unsigned long) sqlite3_column_bytes(stmt, 0);
    uint8_t *string_buffer = GGL_ALLOCN(alloc, uint8_t, value_length);
    if (!string_buffer) {
        GGL_LOGE(
            "read_value_at_key",
            "no more memory to allocate value for key id %ld",
            key_id
        );
        return GGL_ERR_NOMEM;
    }
    value->type = GGL_TYPE_BUF;
    value->buf.data = string_buffer;
    memcpy(string_buffer, value_string, value_length);
    value->buf.len = value_length;

    GGL_LOGD(
        "read_value_at_key",
        "value read: %.*s",
        (int) value->buf.len,
        (char *) value->buf.data
    );
    return GGL_ERR_OK;
}

/// read_key_recursive will read the map or buffer at key_id and store it into
/// value.
// NOLINTNEXTLINE(misc-no-recursion)
static GglError read_key_recursive(
    int64_t key_id, GglObject *value, GglAlloc *alloc
) {
    GGL_LOGD("read_key_recursive", "reading key id %ld", key_id);

    bool value_is_present;
    GglError err = value_is_present_for_key(key_id, &value_is_present);
    if (err != GGL_ERR_OK) {
        return err;
    }
    if (value_is_present) {
        return read_value_at_key(key_id, value, alloc);
    }

    // at this point we know the key should be a map, because it's not a value
    sqlite3_stmt *read_children_stmt;
    sqlite3_prepare_v2(
        config_database,
        "select k.keyId, k.keyvalue from "
        "relationTable r inner join keyTable k on r.keyId = k.keyId "
        "WHERE r.parentid = ?;",
        -1,
        &read_children_stmt,
        NULL
    );
    GGL_DEFER(sqlite3_finalize, read_children_stmt);
    sqlite3_bind_int64(read_children_stmt, 1, key_id);

    // read children count
    size_t children_count = 0;
    while (sqlite3_step(read_children_stmt) == SQLITE_ROW) {
        children_count++;
    }
    if (children_count == 0) {
        GGL_LOGE(
            "read_key_recursive",
            "no value or children keys found for key id %ld",
            key_id
        );
        return GGL_ERR_FAILURE;
    }
    GGL_LOGD(
        "read_key_recursive",
        "the number of children keys for key id %ld is %ld",
        key_id,
        children_count
    );

    // create the kvs for the children
    GglKV *kv_buffer = GGL_ALLOCN(alloc, GglKV, children_count);
    if (!kv_buffer) {
        GGL_LOGE(
            "read_key_recursive",
            "no more memory to allocate kvs for key id %ld",
            key_id
        );
        return GGL_ERR_NOMEM;
    }
    GglKVVec kv_buffer_vec = { .map = (GglMap) { .pairs = kv_buffer, .len = 0 },
                               .capacity = children_count };

    // read the children
    sqlite3_reset(read_children_stmt);
    while (sqlite3_step(read_children_stmt) == SQLITE_ROW) {
        int64_t child_key_id = sqlite3_column_int64(read_children_stmt, 0);
        const uint8_t *child_key_name
            = sqlite3_column_text(read_children_stmt, 1);
        unsigned long child_key_name_length
            = (unsigned long) sqlite3_column_bytes(read_children_stmt, 1);
        uint8_t *child_key_name_memory
            = GGL_ALLOCN(alloc, uint8_t, child_key_name_length);
        if (!child_key_name_memory) {
            GGL_LOGE(
                "read_key_recursive",
                "no more memory to allocate value for key id %ld",
                key_id
            );
            return GGL_ERR_NOMEM;
        }
        memcpy(child_key_name_memory, child_key_name, child_key_name_length);

        GglBuffer child_key_name_buffer
            = { .data = child_key_name_memory, .len = child_key_name_length };
        GglKV child_kv = { .key = child_key_name_buffer };

        read_key_recursive(child_key_id, &child_kv.val, alloc);

        GglError error = ggl_kv_vec_push(&kv_buffer_vec, child_kv);
        if (error != GGL_ERR_OK) {
            GGL_LOGE(
                "read_key_recursive",
                "error pushing kv with error %d",
                (int) error
            );
            return error;
        }
    }

    value->type = GGL_TYPE_MAP;
    value->map = kv_buffer_vec.map;
    return GGL_ERR_OK;
}

GglError ggconfig_get_value_from_key(GglList *key_path, GglObject *value) {
    if (config_initialized == false) {
        return GGL_ERR_FAILURE;
    }

    static uint8_t key_value_memory[GGL_COREBUS_MAX_MSG_LEN];
    GglBumpAlloc bumper = ggl_bump_alloc_init(GGL_BUF(key_value_memory));

    sqlite3_exec(config_database, "BEGIN TRANSACTION", NULL, NULL, NULL);
    GGL_LOGI(
        "ggconfig_get_value_from_key",
        "starting request for key: %s",
        print_key_path(key_path)
    );
    GglObject ids_array[MAX_KEY_PATH_DEPTH];
    GglObjVec ids = { .list = { .items = ids_array, .len = 0 },
                      .capacity = MAX_KEY_PATH_DEPTH };
    GglError err = get_key_ids(key_path, &ids);
    if (err == GGL_ERR_NOENTRY) {
        sqlite3_exec(config_database, "END TRANSACTION", NULL, NULL, NULL);
        return GGL_ERR_NOENTRY;
    }
    if (err != GGL_ERR_OK) {
        sqlite3_exec(config_database, "END TRANSACTION", NULL, NULL, NULL);
        return err;
    }
    int64_t key_id = ids.list.items[ids.list.len - 1].i64;
    err = read_key_recursive(key_id, value, &bumper.alloc);
    sqlite3_exec(config_database, "END TRANSACTION", NULL, NULL, NULL);
    return err;
}

GglError ggconfig_get_key_notification(GglList *key_path, uint32_t handle) {
    GglError return_err = GGL_ERR_FAILURE;

    if (config_initialized == false) {
        return GGL_ERR_FAILURE;
    }

    sqlite3_exec(config_database, "BEGIN TRANSACTION", NULL, NULL, NULL);
    // ensure this key is present in the key path. Key does not require a
    // value
    GglObject ids_array[MAX_KEY_PATH_DEPTH];
    GglObjVec ids = { .list = { .items = ids_array, .len = 0 },
                      .capacity = MAX_KEY_PATH_DEPTH };
    GglError err = get_key_ids(key_path, &ids);
    if (err == GGL_ERR_NOENTRY) {
        sqlite3_exec(config_database, "ROLLBACK", NULL, NULL, NULL);
        return GGL_ERR_NOENTRY;
    }
    if (err != GGL_ERR_OK) {
        sqlite3_exec(config_database, "ROLLBACK", NULL, NULL, NULL);
        return err;
    }
    int64_t key_id = ids.list.items[ids.list.len - 1].i64;

    GGL_LOGI(
        "ggconfig_get_key_notification",
        "Subscribing %d:%d to %s",
        handle && 0xFFFF0000 >> 16,
        handle && 0x0000FFFF,
        print_key_path(key_path)
    );
    // insert the key & handle data into the subscriber database
    GGL_LOGD("ggconfig_get_key_notification", "INSERT %ld, %d", key_id, handle);
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(
        config_database,
        "INSERT INTO subscriberTable(keyid, handle) VALUES (?,?);",
        -1,
        &stmt,
        NULL
    );
    GGL_DEFER(sqlite3_finalize, stmt);
    sqlite3_bind_int64(stmt, 1, key_id);
    sqlite3_bind_int64(stmt, 2, handle);
    int rc = sqlite3_step(stmt);
    sqlite3_exec(config_database, "END TRANSACTION", NULL, NULL, NULL);
    if (SQLITE_DONE != rc) {
        GGL_LOGE(
            "ggconfig_get_key_notification",
            "%d %s",
            rc,
            sqlite3_errmsg(config_database)
        );
    } else {
        GGL_LOGT("ggconfig_get_key_notification", "Success");
        return_err = GGL_ERR_OK;
    }

    return return_err;
}
