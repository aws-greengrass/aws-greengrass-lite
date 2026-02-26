// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "embeds.h"
#include "helpers.h"
#include <assert.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <gg/vector.h>
#include <ggconfigd.h>
#include <ggl/core_bus/constants.h>
#include <ggl/core_bus/server.h>
#include <inttypes.h>
#include <sqlite3.h>
#include <string.h>
#include <stdbool.h>

/// The maximum expected config keys (including nested) held under one component
/// configuration
#define MAX_CONFIG_DESCENDANTS_PER_COMPONENT 256

/// The maximum expected config keys held as children of a single config object
// TODO: Should be at least as big as MAX_COMPONENTS, add static assert?
#define MAX_CONFIG_CHILDREN_PER_OBJECT 64

static inline void cleanup_sqlite3_finalize(sqlite3_stmt **p) {
    if (*p != NULL) {
        sqlite3_finalize(*p);
    }
}

static bool config_initialized = false;
static sqlite3 *config_database;
static const char *config_database_name = "config.db";

static void sqlite_logger(void *ctx, int err_code, const char *str) {
    (void) ctx;
    (void) err_code;
    GG_LOGE("sqlite: %s", str);
}

/// create the database to the correct schema
static GgError create_database(void) {
    GG_LOGI("Initializing new configuration database.");

    // create the initial table
    int result
        = sqlite3_exec(config_database, GGL_SQL_CREATE_DB, NULL, NULL, NULL);
    if (result != SQLITE_OK) {
        GG_LOGI("Error while creating database.");
        return GG_ERR_FAILURE;
    }
    return GG_ERR_OK;
}

GgError ggconfig_open(void) {
    GgError return_err = GG_ERR_FAILURE;
    if (config_initialized == false) {
        int rc = sqlite3_config(SQLITE_CONFIG_LOG, sqlite_logger, NULL);
        if (rc != SQLITE_OK) {
            GG_LOGE("Failed to set sqlite3 logger.");
            return GG_ERR_FAILURE;
        }

        // do configuration
        rc = sqlite3_open(config_database_name, &config_database);
        if (rc) {
            GG_LOGE(
                "Cannot open the configuration database: %s",
                sqlite3_errmsg(config_database)
            );
            return_err = GG_ERR_FAILURE;
        } else {
            GG_LOGI("Config database Opened");

            sqlite3_stmt *stmt;
            sqlite3_prepare_v2( // TODO: We should be checking the return code
                                // of each call to prepare
                config_database,
                GGL_SQL_CHECK_INITALIZED,
                -1,
                &stmt,
                NULL
            );
            GG_CLEANUP(cleanup_sqlite3_finalize, stmt);

            if (sqlite3_step(stmt) == SQLITE_ROW) {
                GG_LOGI("found keyTable");
                return_err = GG_ERR_OK;
            } else {
                return_err = create_database();
                char *err_message = 0;
                rc = sqlite3_exec(
                    config_database,
                    GGL_SQL_CREATE_INDEX,
                    NULL,
                    NULL,
                    &err_message
                );
                if (rc) {
                    GG_LOGI(
                        "Failed to add an index to the relationTable %s, expect an autoindex to be created",
                        err_message
                    );
                    sqlite3_free(err_message);
                }
            }
        }
        // create a temporary table for subscriber data
        char *err_message = 0;
        rc = sqlite3_exec(
            config_database, GGL_SQL_CREATE_SUB_TABLE, NULL, NULL, &err_message
        );
        if (rc) {
            GG_LOGE("Failed to create temporary table %s", err_message);
            sqlite3_free(err_message);
            return_err = GG_ERR_FAILURE;
        }
        config_initialized = true;
    } else {
        return_err = GG_ERR_OK;
    }
    return return_err;
}

GgError ggconfig_close(void) {
    sqlite3_close(config_database);
    config_initialized = false;
    return GG_ERR_OK;
}

static GgError key_insert(GgBuffer *key, int64_t *id_output) {
    GG_LOGT("insert %.*s", (int) key->len, (char *) key->data);
    sqlite3_stmt *key_insert_stmt;
    sqlite3_prepare_v2(
        config_database, GGL_SQL_KEY_INSERT, -1, &key_insert_stmt, NULL
    );
    GG_CLEANUP(cleanup_sqlite3_finalize, key_insert_stmt);
    sqlite3_bind_text(
        key_insert_stmt, 1, (char *) key->data, (int) key->len, SQLITE_STATIC
    );
    if (sqlite3_step(key_insert_stmt) != SQLITE_DONE) {
        GG_LOGE(
            "Failed to insert key: %.*s with error: %s",
            (int) key->len,
            (char *) key->data,
            sqlite3_errmsg(config_database)
        );
        return GG_ERR_FAILURE;
    }
    *id_output = sqlite3_last_insert_rowid(config_database);
    GG_LOGT(
        "Insert %.*s result: %" PRId64, (int) key->len, key->data, *id_output
    );
    return GG_ERR_OK;
}

static GgError value_is_present_for_key(
    int64_t key_id, bool *value_is_present_output
) {
    GG_LOGT("Checking id %" PRId64, key_id);

    sqlite3_stmt *find_value_stmt;
    sqlite3_prepare_v2(
        config_database, GGL_SQL_VALUE_PRESENT, -1, &find_value_stmt, NULL
    );
    GG_CLEANUP(cleanup_sqlite3_finalize, find_value_stmt);
    sqlite3_bind_int64(find_value_stmt, 1, key_id);
    int rc = sqlite3_step(find_value_stmt);
    if (rc == SQLITE_ROW) {
        int64_t pid = sqlite3_column_int(find_value_stmt, 0);
        if (pid) {
            GG_LOGT("Id %" PRId64 " does have a value", key_id);
            *value_is_present_output = true;
            return GG_ERR_OK;
        }
        GG_LOGE(
            "Checking presence of value for key id %" PRId64 " failed", key_id
        );
        return GG_ERR_FAILURE;
    }
    if (rc == SQLITE_DONE) {
        GG_LOGT("Id %" PRId64 " does not have a value", key_id);
        *value_is_present_output = false;
        return GG_ERR_OK;
    }
    GG_LOGE(
        "Checking presence of value for key id %" PRId64
        " failed with error: %s",
        key_id,
        sqlite3_errmsg(config_database)
    );
    return GG_ERR_FAILURE;
}

static GgError find_key_with_parent(
    GgBuffer *key, int64_t parent_key_id, int64_t *key_id_output
) {
    int64_t id = 0;
    GG_LOGT(
        "searching for key %.*s with parent id %" PRId64,
        (int) key->len,
        key->data,
        parent_key_id
    );
    sqlite3_stmt *find_element_stmt;
    sqlite3_prepare_v2(
        config_database,
        GGL_SQL_GET_KEY_WITH_PARENT,
        -1,
        &find_element_stmt,
        NULL
    );
    GG_CLEANUP(cleanup_sqlite3_finalize, find_element_stmt);
    sqlite3_bind_text(
        find_element_stmt, 1, (char *) key->data, (int) key->len, SQLITE_STATIC
    );
    sqlite3_bind_int64(find_element_stmt, 2, parent_key_id);
    int rc = sqlite3_step(find_element_stmt);
    GG_LOGT("find element returned %d", rc);
    if (rc == SQLITE_ROW) {
        id = sqlite3_column_int(find_element_stmt, 0);
        GG_LOGT(
            "found key %.*s with parent id %" PRId64 " at %" PRId64,
            (int) key->len,
            key->data,
            parent_key_id,
            id
        );
        *key_id_output = id;
        return GG_ERR_OK;
    }
    if (rc == SQLITE_DONE) {
        GG_LOGT(
            "key %.*s with parent id %" PRId64 " not found",
            (int) key->len,
            key->data,
            parent_key_id
        );
        return GG_ERR_NOENTRY;
    }
    GG_LOGE(
        "finding key %.*s with parent id %" PRId64 " failed with error: %s",
        (int) key->len,
        key->data,
        parent_key_id,
        sqlite3_errmsg(config_database)
    );
    return GG_ERR_FAILURE;
}

// get or create a keyid where the key is a root (first element of a path)
static GgError get_or_create_key_at_root(GgBuffer *key, int64_t *id_output) {
    GG_LOGT("Checking %.*s", (int) key->len, (char *) key->data);
    int64_t id = 0;

    sqlite3_stmt *root_check_stmt;
    sqlite3_prepare_v2(
        config_database, GGL_SQL_GET_ROOT_KEY, -1, &root_check_stmt, NULL
    );
    GG_CLEANUP(cleanup_sqlite3_finalize, root_check_stmt);
    sqlite3_bind_text(
        root_check_stmt, 1, (char *) key->data, (int) key->len, SQLITE_STATIC
    );
    int rc = sqlite3_step(root_check_stmt);
    if (rc == SQLITE_ROW) { // exists as a root and here is the id
        id = sqlite3_column_int(root_check_stmt, 0);
        GG_LOGT("Found %.*s at %" PRId64, (int) key->len, key->data, id);
    } else if (rc == SQLITE_DONE) { // doesn't exist at root, so we need to
                                    // create the key and get the id
        GgError err = key_insert(key, &id);
        if (err != GG_ERR_OK) {
            return GG_ERR_FAILURE;
        }
    } else {
        GG_LOGE(
            "finding key %.*s failed with error: %s",
            (int) key->len,
            key->data,
            sqlite3_errmsg(config_database)
        );
        return GG_ERR_FAILURE;
    }
    *id_output = id;
    return GG_ERR_OK;
}

static GgError relation_insert(int64_t id, int64_t parent) {
    sqlite3_stmt *relation_insert_stmt;
    sqlite3_prepare_v2(
        config_database,
        GGL_SQL_INSERT_RELATION,
        -1,
        &relation_insert_stmt,
        NULL
    );
    GG_CLEANUP(cleanup_sqlite3_finalize, relation_insert_stmt);
    sqlite3_bind_int64(relation_insert_stmt, 1, id);
    sqlite3_bind_int64(relation_insert_stmt, 2, parent);
    int rc = sqlite3_step(relation_insert_stmt);
    if (rc == SQLITE_DONE || rc == SQLITE_OK) {
        GG_LOGT(
            "relation insert successful key:%" PRId64 ", parent:%" PRId64,
            id,
            parent
        );
    } else {
        GG_LOGE("relation insert fail: %s", sqlite3_errmsg(config_database));
        return GG_ERR_FAILURE;
    }
    return GG_ERR_OK;
}

static GgError value_insert(
    int64_t key_id, GgBuffer *value, int64_t timestamp
) {
    GgError return_err = GG_ERR_FAILURE;
    sqlite3_stmt *value_insert_stmt;
    sqlite3_prepare_v2(
        config_database, GGL_SQL_VALUE_INSERT, -1, &value_insert_stmt, NULL
    );
    GG_CLEANUP(cleanup_sqlite3_finalize, value_insert_stmt);
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
        GG_LOGT("value insert successful");
        return_err = GG_ERR_OK;
    } else {
        GG_LOGE(
            "value insert fail with rc %d and error %s",
            rc,
            sqlite3_errmsg(config_database)
        );
        return_err = GG_ERR_FAILURE;
    }
    return return_err;
}

static GgError value_update(
    int64_t key_id, GgBuffer *value, int64_t timestamp
) {
    GgError return_err = GG_ERR_FAILURE;

    sqlite3_stmt *update_value_stmt;
    sqlite3_prepare_v2(
        config_database, GGL_SQL_VALUE_UPDATE, -1, &update_value_stmt, NULL
    );
    GG_CLEANUP(cleanup_sqlite3_finalize, update_value_stmt);
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
        GG_LOGT("value update successful");
        return_err = GG_ERR_OK;
    } else {
        GG_LOGE(
            "value update fail with rc %d and error %s",
            rc,
            sqlite3_errmsg(config_database)
        );
        return_err = GG_ERR_FAILURE;
    }
    return return_err;
}

static GgError value_get_timestamp(
    int64_t id, int64_t *existing_timestamp_output
) {
    sqlite3_stmt *get_timestamp_stmt;
    sqlite3_prepare_v2(
        config_database, GGL_SQL_GET_TIMESTAMP, -1, &get_timestamp_stmt, NULL
    );
    GG_CLEANUP(cleanup_sqlite3_finalize, get_timestamp_stmt);
    sqlite3_bind_int64(get_timestamp_stmt, 1, id);
    int rc = sqlite3_step(get_timestamp_stmt);
    if (rc == SQLITE_ROW) {
        int64_t timestamp = sqlite3_column_int64(get_timestamp_stmt, 0);
        *existing_timestamp_output = timestamp;
        return GG_ERR_OK;
    }
    if (rc == SQLITE_DONE) {
        return GG_ERR_NOENTRY;
    }
    GG_LOGE(
        "getting timestamp for id %" PRId64 " failed with error: %s",
        id,
        sqlite3_errmsg(config_database)
    );
    return GG_ERR_FAILURE;
}

// key_ids_output must point to an empty GgObjVec with capacity
// GG_MAX_OBJECT_DEPTH
static GgError get_key_ids(GgList *key_path, GgObjVec *key_ids_output) {
    GG_LOGT("searching for %s", print_key_path(key_path));

    sqlite3_stmt *find_element_stmt;
    sqlite3_prepare_v2(
        config_database, GGL_SQL_FIND_ELEMENT, -1, &find_element_stmt, NULL
    );
    GG_CLEANUP(cleanup_sqlite3_finalize, find_element_stmt);

    for (size_t index = 0; index < key_path->len; index++) {
        GgBuffer key = gg_obj_into_buf(key_path->items[index]);
        sqlite3_bind_text(
            find_element_stmt,
            (int) index + 1,
            (char *) key.data,
            (int) key.len,
            SQLITE_STATIC
        );
    }

    for (size_t index = key_path->len; index < GG_MAX_OBJECT_DEPTH; index++) {
        sqlite3_bind_null(find_element_stmt, (int) index + 1);
    }

    sqlite3_bind_int(
        find_element_stmt, GG_MAX_OBJECT_DEPTH + 1, (int) key_path->len
    );

    for (size_t i = 0; i < key_path->len; i++) {
        int rc = sqlite3_step(find_element_stmt);
        if (rc == SQLITE_DONE) {
            GG_LOGT(
                "id not found for key %d in %s",
                (int) i,
                print_key_path(key_path)
            );
            return GG_ERR_NOENTRY;
        }
        if (rc != SQLITE_ROW) {
            GG_LOGE(
                "get key id for key %d in %s fail: %s",
                (int) i,
                print_key_path(key_path),
                sqlite3_errmsg(config_database)
            );
            return GG_ERR_FAILURE;
        }
        int64_t id = sqlite3_column_int(find_element_stmt, 0);
        GG_LOGT(
            "found id for key %d in %s: %" PRId64,
            (int) i,
            print_key_path(key_path),
            id
        );
        GgError ret = gg_obj_vec_push(key_ids_output, gg_obj_i64(id));
        assert(ret == GG_ERR_OK);
    }

    return GG_ERR_OK;
}

// create_key_path assumes that the entire key_path does not already exist in
// the database (i.e. at least one key needs to be created). Behavior is
// undefined if the key_path fully exists already. Thus it should only be used
// within a transaction and after checking that the key_path does not fully
// exist.
// key_ids_output must point to an empty GgObjVec with capacity
// MAX_KEY_PATH_DEPTH
static GgError create_key_path(GgList *key_path, GgObjVec *key_ids_output) {
    GgBuffer root_key_buffer = gg_obj_into_buf(key_path->items[0]);
    int64_t parent_key_id;
    GgError err = get_or_create_key_at_root(&root_key_buffer, &parent_key_id);
    if (err != GG_ERR_OK) {
        return err;
    }
    err = gg_obj_vec_push(key_ids_output, gg_obj_i64(parent_key_id));
    assert(err == GG_ERR_OK);
    bool value_is_present_for_root_key;
    err = value_is_present_for_key(
        parent_key_id, &value_is_present_for_root_key
    );
    if (err != GG_ERR_OK) {
        GG_LOGE(
            "failed to check for value for root key %.*s with id %" PRId64
            " with error %s",
            (int) root_key_buffer.len,
            root_key_buffer.data,
            parent_key_id,
            gg_strerror(err)
        );
        return err;
    }
    if (value_is_present_for_root_key) {
        GG_LOGW(
            "value already present for root key %.*s with id %" PRId64
            ". Failing request.",
            (int) root_key_buffer.len,
            root_key_buffer.data,
            parent_key_id
        );
        return GG_ERR_FAILURE;
    }

    int64_t current_key_id = parent_key_id;
    for (size_t index = 1; index < key_path->len; index++) {
        GgBuffer current_key_buffer = gg_obj_into_buf(key_path->items[index]);
        err = find_key_with_parent(
            &current_key_buffer, parent_key_id, &current_key_id
        );
        if (err == GG_ERR_NOENTRY) {
            err = key_insert(&current_key_buffer, &current_key_id);
            if (err != GG_ERR_OK) {
                return err;
            }
            err = relation_insert(current_key_id, parent_key_id);
            if (err != GG_ERR_OK) {
                return err;
            }
        } else if (err == GG_ERR_OK) { // the key exists and we got the id
            bool value_is_present;
            err = value_is_present_for_key(current_key_id, &value_is_present);
            if (err != GG_ERR_OK) {
                GG_LOGE(
                    "failed to check for value for key %d (%.*s) in key path %s with id %" PRId64
                    " with error %s",
                    (int) index,
                    (int) current_key_buffer.len,
                    current_key_buffer.data,
                    print_key_path(key_path),
                    current_key_id,
                    gg_strerror(err)
                );
                return err;
            }
            if (value_is_present) {
                GG_LOGW(
                    "value already present for key %d (%.*s) in key path %s with id %" PRId64
                    ". Failing request.",
                    (int) index,
                    (int) current_key_buffer.len,
                    current_key_buffer.data,
                    print_key_path(key_path),
                    current_key_id
                );
                return GG_ERR_FAILURE;
            }
        } else {
            return err;
        }
        err = gg_obj_vec_push(key_ids_output, gg_obj_i64(current_key_id));
        assert(err == GG_ERR_OK);
        parent_key_id = current_key_id;
    }
    return GG_ERR_OK;
}

static GgError child_is_present_for_key(
    int64_t key_id, bool *child_is_present_output
) {
    GgError return_err = GG_ERR_FAILURE;

    sqlite3_stmt *child_check_stmt;
    sqlite3_prepare_v2(
        config_database, GGL_SQL_HAS_CHILD, -1, &child_check_stmt, NULL
    );
    GG_CLEANUP(cleanup_sqlite3_finalize, child_check_stmt);
    sqlite3_bind_int64(child_check_stmt, 1, key_id);
    int rc = sqlite3_step(child_check_stmt);
    if (rc == SQLITE_ROW) {
        *child_is_present_output = true;
        return_err = GG_ERR_OK;
    } else if (rc == SQLITE_DONE) {
        *child_is_present_output = false;
        return_err = GG_ERR_OK;
    } else {
        GG_LOGE("child check fail : %s", sqlite3_errmsg(config_database));
        return_err = GG_ERR_FAILURE;
    }
    return return_err;
}

static GgError notify_single_key(
    int64_t notify_key_id, GgList *changed_key_path
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
        config_database, GGL_SQL_GET_SUBSCRIBERS, -1, &stmt, NULL
    );
    GG_CLEANUP(cleanup_sqlite3_finalize, stmt);
    sqlite3_bind_int64(stmt, 1, notify_key_id);
    int rc = 0;
    GG_LOGT(
        "notifying subscribers on key with id %" PRId64
        " that key %s has changed",
        notify_key_id,
        print_key_path(changed_key_path)
    );
    do {
        rc = sqlite3_step(stmt);
        switch (rc) {
        case SQLITE_DONE:
            GG_LOGT("DONE");
            break;
        case SQLITE_ROW: {
            uint32_t handle = (uint32_t) sqlite3_column_int64(stmt, 0);
            GG_LOGT("Sending to %u", handle);
            ggl_sub_respond(handle, gg_obj_list(*changed_key_path));
        } break;
        default:
            GG_LOGE(
                "Unexpected rc %d while getting handles to notify for key with id %" PRId64
                " with error: %s",
                rc,
                notify_key_id,
                sqlite3_errmsg(config_database)
            );
            return GG_ERR_FAILURE;
            break;
        }
    } while (rc == SQLITE_ROW);

    return GG_ERR_OK;
}

// Given a key path and the ids of the keys in that path, notify each key along
// the path that the value at the tip of the key path has changed
static GgError notify_nested_key(GgList *key_path, GgObjVec key_ids) {
    for (size_t i = 0; i < key_ids.list.len; i++) {
        GgError ret = notify_single_key(
            gg_obj_into_i64(key_ids.list.items[i]), key_path
        );
        if (ret != GG_ERR_OK) {
            return ret;
        }
    }
    return GG_ERR_OK;
}

GgError ggconfig_write_empty_map(GgList *key_path) {
    if (config_initialized == false) {
        GG_LOGE("Database not initialized");
        return GG_ERR_FAILURE;
    }

    sqlite3_exec(config_database, "BEGIN TRANSACTION", NULL, NULL, NULL);
    GG_LOGT(
        "Starting transaction to write an empty map to key %s",
        print_key_path(key_path)
    );

    GgObject ids_array[GG_MAX_OBJECT_DEPTH];
    GgObjVec ids = { .list = { .items = ids_array, .len = 0 },
                     .capacity = GG_MAX_OBJECT_DEPTH };
    int64_t last_key_id;
    GgError err = get_key_ids(key_path, &ids);
    if (err == GG_ERR_NOENTRY) {
        ids.list.len = 0; // Reset the ids vector to be populated fresh
        err = create_key_path(key_path, &ids);
        if (err != GG_ERR_OK) {
            sqlite3_exec(config_database, "ROLLBACK", NULL, NULL, NULL);
            return err;
        }
        sqlite3_exec(config_database, "END TRANSACTION", NULL, NULL, NULL);
        return GG_ERR_OK;
    }
    if (err != GG_ERR_OK) {
        GG_LOGE(
            "Failed to get key ids for key path %s with error %s",
            print_key_path(key_path),
            gg_strerror(err)
        );
        sqlite3_exec(config_database, "ROLLBACK", NULL, NULL, NULL);
        return err;
    }

    last_key_id = gg_obj_into_i64(ids.list.items[ids.list.len - 1]);

    bool value_is_present;
    err = value_is_present_for_key(last_key_id, &value_is_present);
    if (err != GG_ERR_OK) {
        sqlite3_exec(config_database, "ROLLBACK", NULL, NULL, NULL);
        return err;
    }
    if (value_is_present) {
        GG_LOGW(
            "Value already present for key %s with id %" PRId64
            ", so an empty map can not be merged. Failing request.",
            print_key_path(key_path),
            last_key_id
        );
        sqlite3_exec(config_database, "ROLLBACK", NULL, NULL, NULL);
        return GG_ERR_FAILURE;
    }

    sqlite3_exec(config_database, "END TRANSACTION", NULL, NULL, NULL);
    return GG_ERR_OK;
}

GgError ggconfig_write_value_at_key(
    GgList *key_path, GgBuffer *value, int64_t timestamp
) {
    if (config_initialized == false) {
        GG_LOGE("Database not initialized");
        return GG_ERR_FAILURE;
    }

    sqlite3_exec(config_database, "BEGIN TRANSACTION", NULL, NULL, NULL);
    GG_LOGT(
        "starting transaction to insert/update key: %s",
        print_key_path(key_path)
    );

    GgObject ids_array[GG_MAX_OBJECT_DEPTH];
    GgObjVec ids = { .list = { .items = ids_array, .len = 0 },
                     .capacity = GG_MAX_OBJECT_DEPTH };
    int64_t last_key_id;
    GgError err = get_key_ids(key_path, &ids);
    if (err == GG_ERR_NOENTRY) {
        ids.list.len = 0; // Reset the ids vector to be populated fresh
        err = create_key_path(key_path, &ids);
        if (err != GG_ERR_OK) {
            sqlite3_exec(config_database, "ROLLBACK", NULL, NULL, NULL);
            return err;
        }

        last_key_id = gg_obj_into_i64(ids.list.items[ids.list.len - 1]);
        err = value_insert(last_key_id, value, timestamp);
        if (err != GG_ERR_OK) {
            sqlite3_exec(config_database, "ROLLBACK", NULL, NULL, NULL);
            return err;
        }
        sqlite3_exec(config_database, "END TRANSACTION", NULL, NULL, NULL);
        err = notify_nested_key(key_path, ids);
        if (err != GG_ERR_OK) {
            GG_LOGE(
                "Failed to notify all subscribers about update for key path %s with error %s",
                print_key_path(key_path),
                gg_strerror(err)
            );
        }
        return GG_ERR_OK;
    }
    if (err != GG_ERR_OK) {
        GG_LOGE(
            "Failed to get key ids for key path %s with error %s",
            print_key_path(key_path),
            gg_strerror(err)
        );
        sqlite3_exec(config_database, "ROLLBACK", NULL, NULL, NULL);
        return err;
    }
    last_key_id = gg_obj_into_i64(ids.list.items[ids.list.len - 1]);
    bool child_is_present;
    err = child_is_present_for_key(last_key_id, &child_is_present);
    if (err != GG_ERR_OK) {
        GG_LOGE(
            "Failed to check for child presence for key %s with id %" PRId64
            " with error %s",
            print_key_path(key_path),
            last_key_id,
            gg_strerror(err)
        );
        sqlite3_exec(config_database, "ROLLBACK", NULL, NULL, NULL);
        return err;
    }
    if (child_is_present) {
        GG_LOGW(
            "Key %s with id %" PRId64
            " is an object with one or more children, so it can not also store a value. Failing request.",
            print_key_path(key_path),
            last_key_id
        );
        sqlite3_exec(config_database, "ROLLBACK", NULL, NULL, NULL);
        return GG_ERR_FAILURE;
    }

    bool value_is_present;
    err = value_is_present_for_key(last_key_id, &value_is_present);
    if (err != GG_ERR_OK) {
        sqlite3_exec(config_database, "ROLLBACK", NULL, NULL, NULL);
        return err;
    }
    if (!value_is_present) {
        GG_LOGW(
            "Key %s with id %" PRId64
            " is an empty map, so it can not have a value written to it. Failing request.",
            print_key_path(key_path),
            last_key_id
        );
        sqlite3_exec(config_database, "ROLLBACK", NULL, NULL, NULL);
        return GG_ERR_FAILURE;
    }

    int64_t existing_timestamp;
    err = value_get_timestamp(last_key_id, &existing_timestamp);
    if (err != GG_ERR_OK) {
        GG_LOGE(
            "failed to get timestamp for key %s with id %" PRId64
            " with error %s",
            print_key_path(key_path),
            last_key_id,
            gg_strerror(err)
        );
        sqlite3_exec(config_database, "ROLLBACK", NULL, NULL, NULL);
        return err;
    }
    if (existing_timestamp > timestamp) {
        GG_LOGD(
            "key %s has an existing timestamp %" PRId64
            " newer than provided timestamp %" PRId64
            ", so it will not be updated",
            print_key_path(key_path),
            existing_timestamp,
            timestamp
        );
        sqlite3_exec(config_database, "END TRANSACTION", NULL, NULL, NULL);
        return GG_ERR_OK;
    }

    err = value_update(last_key_id, value, timestamp);
    if (err != GG_ERR_OK) {
        GG_LOGE(
            "failed to update value for key %s with id %" PRId64
            " with error %s",
            print_key_path(key_path),
            last_key_id,
            gg_strerror(err)
        );
        sqlite3_exec(config_database, "ROLLBACK", NULL, NULL, NULL);
        return err;
    }
    sqlite3_exec(config_database, "END TRANSACTION", NULL, NULL, NULL);

    err = notify_nested_key(key_path, ids);
    if (err != GG_ERR_OK) {
        GG_LOGE(
            "failed to notify subscribers about update for key path %s with error %s",
            print_key_path(key_path),
            gg_strerror(err)
        );
    }
    return GG_ERR_OK;
}

static GgError read_value_at_key(
    int64_t key_id, GgObject *value, GgArena *alloc
) {
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(config_database, GGL_SQL_READ_VALUE, -1, &stmt, NULL);
    GG_CLEANUP(cleanup_sqlite3_finalize, stmt);
    sqlite3_bind_int64(stmt, 1, key_id);
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE) {
        GG_LOGI("no value found for key id %" PRId64, key_id);
        return GG_ERR_NOENTRY;
    }
    if (rc != SQLITE_ROW) {
        GG_LOGE(
            "failed to read value for key id %" PRId64
            " with rc %d and error %s",
            key_id,
            rc,
            sqlite3_errmsg(config_database)
        );
        return GG_ERR_FAILURE;
    }
    const uint8_t *value_string = sqlite3_column_text(stmt, 0);
    unsigned long value_length = (unsigned long) sqlite3_column_bytes(stmt, 0);
    uint8_t *string_buffer = GG_ARENA_ALLOCN(alloc, uint8_t, value_length);
    if (!string_buffer) {
        GG_LOGE("no more memory to allocate value for key id %" PRId64, key_id);
        return GG_ERR_NOMEM;
    }
    memcpy(string_buffer, value_string, value_length);
    *value
        = gg_obj_buf((GgBuffer) { .data = string_buffer, .len = value_length });
    return GG_ERR_OK;
}

/// read_key_recursive will read the map or buffer at key_id and store it into
/// value.
// NOLINTNEXTLINE(misc-no-recursion)
static GgError read_key_recursive(
    int64_t key_id, GgObject *value, GgArena *alloc
) {
    GG_LOGT("reading key id %" PRId64, key_id);

    bool value_is_present;
    GgError ret = value_is_present_for_key(key_id, &value_is_present);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    if (value_is_present) {
        ret = read_value_at_key(key_id, value, alloc);
        GG_LOGT(
            "value read: %.*s from key id %" PRId64,
            (int) gg_obj_into_buf(*value).len,
            (char *) gg_obj_into_buf(*value).data,
            key_id
        );
        return ret;
    }

    // at this point we know the key should be a map, because it's not a value
    sqlite3_stmt *read_children_stmt;
    sqlite3_prepare_v2(
        config_database, GGL_SQL_GET_CHILDREN, -1, &read_children_stmt, NULL
    );
    GG_CLEANUP(cleanup_sqlite3_finalize, read_children_stmt);
    sqlite3_bind_int64(read_children_stmt, 1, key_id);

    // read children count
    size_t children_count = 0;
    int rc = sqlite3_step(read_children_stmt);
    while (rc == SQLITE_ROW) {
        children_count++;
        rc = sqlite3_step(read_children_stmt);
    }
    if (rc != SQLITE_DONE) {
        GG_LOGE(
            "failed to read children count for key id %" PRId64
            " with rc %d and error %s",
            key_id,
            rc,
            sqlite3_errmsg(config_database)
        );
        return GG_ERR_FAILURE;
    }
    GG_LOGT(
        "the number of children keys for key id %" PRId64 " is %zd",
        key_id,
        children_count
    );
    if (children_count == 0) {
        *value = gg_obj_map((GgMap) { 0 });
        GG_LOGT("value read: empty map for key id %" PRId64, key_id);
        return GG_ERR_OK;
    }

    // create the kvs for the children
    GgKV *kv_buffer = GG_ARENA_ALLOCN(alloc, GgKV, children_count);
    if (!kv_buffer) {
        GG_LOGE("no more memory to allocate kvs for key id %" PRId64, key_id);
        return GG_ERR_NOMEM;
    }
    GgKVVec kv_buffer_vec = { .map = (GgMap) { .pairs = kv_buffer, .len = 0 },
                              .capacity = children_count };

    // read the children
    sqlite3_reset(read_children_stmt);
    rc = sqlite3_step(read_children_stmt);
    while (rc == SQLITE_ROW) {
        int64_t child_key_id = sqlite3_column_int64(read_children_stmt, 0);
        const uint8_t *child_key_name
            = sqlite3_column_text(read_children_stmt, 1);
        unsigned long child_key_name_length
            = (unsigned long) sqlite3_column_bytes(read_children_stmt, 1);
        uint8_t *child_key_name_memory
            = GG_ARENA_ALLOCN(alloc, uint8_t, child_key_name_length);
        if (!child_key_name_memory) {
            GG_LOGE(
                "no more memory to allocate value for key id %" PRId64, key_id
            );
            return GG_ERR_NOMEM;
        }
        memcpy(child_key_name_memory, child_key_name, child_key_name_length);

        GgBuffer child_key_name_buffer
            = { .data = child_key_name_memory, .len = child_key_name_length };
        GgKV child_kv = gg_kv(child_key_name_buffer, GG_OBJ_NULL);

        ret = read_key_recursive(child_key_id, gg_kv_val(&child_kv), alloc);
        if (ret != GG_ERR_OK) {
            return ret;
        }

        ret = gg_kv_vec_push(&kv_buffer_vec, child_kv);
        if (ret != GG_ERR_OK) {
            GG_LOGE("error pushing kv with error %s", gg_strerror(ret));
            return ret;
        }

        rc = sqlite3_step(read_children_stmt);
    }
    if (rc != SQLITE_DONE) {
        GG_LOGE(
            "failed to read children for key id %" PRId64
            " with rc %d and error %s",
            key_id,
            rc,
            sqlite3_errmsg(config_database)
        );
        return GG_ERR_FAILURE;
    }

    *value = gg_obj_map(kv_buffer_vec.map);
    return GG_ERR_OK;
}

GgError ggconfig_get_value_from_key(GgList *key_path, GgObject *value) {
    if (config_initialized == false) {
        GG_LOGE("Database not initialized.");
        return GG_ERR_FAILURE;
    }

    static uint8_t key_value_memory[GGL_COREBUS_MAX_MSG_LEN];
    GgArena alloc = gg_arena_init(GG_BUF(key_value_memory));

    sqlite3_exec(config_database, "BEGIN TRANSACTION", NULL, NULL, NULL);
    GG_LOGT("Starting transaction to read key: %s", print_key_path(key_path));

    GgObject ids_array[GG_MAX_OBJECT_DEPTH];
    GgObjVec ids = { .list = { .items = ids_array, .len = 0 },
                     .capacity = GG_MAX_OBJECT_DEPTH };
    GgError err = get_key_ids(key_path, &ids);
    if (err == GG_ERR_NOENTRY) {
        sqlite3_exec(config_database, "END TRANSACTION", NULL, NULL, NULL);
        return GG_ERR_NOENTRY;
    }
    if (err != GG_ERR_OK) {
        sqlite3_exec(config_database, "END TRANSACTION", NULL, NULL, NULL);
        return err;
    }
    int64_t key_id = gg_obj_into_i64(ids.list.items[ids.list.len - 1]);
    err = read_key_recursive(key_id, value, &alloc);
    sqlite3_exec(config_database, "END TRANSACTION", NULL, NULL, NULL);
    return err;
}

static GgError get_children(
    int64_t key_id, GgObjVec *children_ids_output, GgArena *alloc
) {
    GG_LOGT("Getting children for id %" PRId64, key_id);

    sqlite3_stmt *read_children_stmt;
    sqlite3_prepare_v2(
        config_database, GGL_SQL_GET_CHILDREN, -1, &read_children_stmt, NULL
    );
    GG_CLEANUP(cleanup_sqlite3_finalize, read_children_stmt);
    sqlite3_bind_int64(read_children_stmt, 1, key_id);

    int rc = sqlite3_step(read_children_stmt);
    while (rc == SQLITE_ROW) {
        const uint8_t *child_key_name
            = sqlite3_column_text(read_children_stmt, 1);
        unsigned long child_key_name_length
            = (unsigned long) sqlite3_column_bytes(read_children_stmt, 1);

        GG_LOGT("Found child.");
        uint8_t *child_key_name_memory
            = GG_ARENA_ALLOCN(alloc, uint8_t, child_key_name_length);
        if (!child_key_name_memory) {
            GG_LOGE("No more memory to allocate while reading children keys.");
            return GG_ERR_NOMEM;
        }

        memcpy(child_key_name_memory, child_key_name, child_key_name_length);

        GgError err = gg_obj_vec_push(
            children_ids_output,
            gg_obj_buf((GgBuffer) { .data = child_key_name_memory,
                                    .len = child_key_name_length })
        );
        if (err != GG_ERR_OK) {
            GG_LOGE("Not enough memory to push a child into the output vector."
            );
            return err;
        }
        rc = sqlite3_step(read_children_stmt);
    }
    if (rc != SQLITE_DONE) {
        GG_LOGE(
            "Get children for key id %" PRId64
            " failed with rc: %d and msg: %s",
            key_id,
            rc,
            sqlite3_errmsg(config_database)
        );
        return GG_ERR_FAILURE;
    }
    return GG_ERR_OK;
}

GgError ggconfig_list_subkeys(GgList *key_path, GgList *subkeys) {
    if (config_initialized == false) {
        GG_LOGE("Database not initialized.");
        return GG_ERR_FAILURE;
    }

    sqlite3_exec(config_database, "BEGIN TRANSACTION", NULL, NULL, NULL);
    GG_LOGT(
        "Starting transaction to read subkeys for key: %s",
        print_key_path(key_path)
    );

    GgObject ids_array[GG_MAX_OBJECT_DEPTH];
    GgObjVec ids = { .list = { .items = ids_array, .len = 0 },
                     .capacity = GG_MAX_OBJECT_DEPTH };
    GgError err = get_key_ids(key_path, &ids);
    if (err == GG_ERR_NOENTRY) {
        sqlite3_exec(config_database, "END TRANSACTION", NULL, NULL, NULL);
        return GG_ERR_NOENTRY;
    }
    if (err != GG_ERR_OK) {
        sqlite3_exec(config_database, "END TRANSACTION", NULL, NULL, NULL);
        return err;
    }
    int64_t key_id = gg_obj_into_i64(ids.list.items[ids.list.len - 1]);

    bool value_is_present;
    err = value_is_present_for_key(key_id, &value_is_present);
    if (err != GG_ERR_OK) {
        sqlite3_exec(config_database, "END TRANSACTION", NULL, NULL, NULL);
        return err;
    }
    if (value_is_present) {
        GG_LOGW(
            "Key %s is a value, not a map, so subkeys/children can not be listed.",
            print_key_path(key_path)
        );
        sqlite3_exec(config_database, "END TRANSACTION", NULL, NULL, NULL);
        return GG_ERR_INVALID;
    }

    static GgObject children_ids_array[MAX_CONFIG_CHILDREN_PER_OBJECT];
    GgObjVec children_ids = { .list = { .items = children_ids_array, .len = 0 },
                              .capacity = MAX_CONFIG_CHILDREN_PER_OBJECT };

    static uint8_t key_buffers_memory[GGL_COREBUS_MAX_MSG_LEN]; // TODO: can we
                                                                // shrink this?
    GgArena alloc = gg_arena_init(GG_BUF(key_buffers_memory));
    err = get_children(key_id, &children_ids, &alloc);
    if (err != GG_ERR_OK) {
        sqlite3_exec(config_database, "END TRANSACTION", NULL, NULL, NULL);
        return err;
    }

    sqlite3_exec(config_database, "END TRANSACTION", NULL, NULL, NULL);
    subkeys->items = children_ids.list.items;
    subkeys->len = children_ids.list.len;
    return GG_ERR_OK;
}

/// read all the descendants of key_id, including key_id itself as a descendant
static GgError get_descendants(
    int64_t key_id, GgObjVec *descendant_ids_output
) {
    GG_LOGT("getting descendants for id %" PRId64, key_id);
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(
        config_database, GGL_SQL_GET_DESCENDANTS, -1, &stmt, NULL
    );
    GG_CLEANUP(cleanup_sqlite3_finalize, stmt);
    sqlite3_bind_int64(stmt, 1, key_id);
    sqlite3_bind_int64(stmt, 2, key_id);

    int rc = sqlite3_step(stmt);
    while (rc == SQLITE_ROW) {
        int64_t id = sqlite3_column_int64(stmt, 0);
        GG_LOGT("found descendant id %" PRId64, id);
        GgError err = gg_obj_vec_push(descendant_ids_output, gg_obj_i64(id));
        if (err != GG_ERR_OK) {
            GG_LOGE(
                "Not enough memory to push a descendant into the output vector."
            );
            return err;
        }
        rc = sqlite3_step(stmt);
    }
    if (rc != SQLITE_DONE) {
        GG_LOGE(
            "get descendants for key id %" PRId64 " fail: %s",
            key_id,
            sqlite3_errmsg(config_database)
        );
        return GG_ERR_FAILURE;
    }
    return GG_ERR_OK;
}

static GgError delete_value(int64_t key_id) {
    GG_LOGT("Deleting key id %" PRId64 " from the value table", key_id);
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(config_database, GGL_SQL_DELETE_VALUE, -1, &stmt, NULL);
    GG_CLEANUP(cleanup_sqlite3_finalize, stmt);
    sqlite3_bind_int64(stmt, 1, key_id);
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        GG_LOGE(
            "delete value for key id %" PRId64 " fail: %s",
            key_id,
            sqlite3_errmsg(config_database)
        );
        return GG_ERR_FAILURE;
    }
    return GG_ERR_OK;
}

static GgError delete_relations(int64_t key_id) {
    GG_LOGT(
        "Deleting all entries referencing key id %" PRId64
        " from the relation table",
        key_id
    );
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(
        config_database, GGL_SQL_DELETE_RELATIONS, -1, &stmt, NULL
    );
    GG_CLEANUP(cleanup_sqlite3_finalize, stmt);
    sqlite3_bind_int64(stmt, 1, key_id);
    sqlite3_bind_int64(stmt, 2, key_id);
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        GG_LOGE(
            "delete relations for key id %" PRId64 " fail: %s",
            key_id,
            sqlite3_errmsg(config_database)
        );
        return GG_ERR_FAILURE;
    }
    return GG_ERR_OK;
}

static GgError delete_subscribers(int64_t key_id) {
    GG_LOGT("Deleting key id %" PRId64 " from the subscribers table", key_id);
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(
        config_database, GGL_SQL_DELETE_SUBSCRIBERS, -1, &stmt, NULL
    );
    GG_CLEANUP(cleanup_sqlite3_finalize, stmt);
    sqlite3_bind_int64(stmt, 1, key_id);
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        GG_LOGE(
            "delete subscribers on keyid %" PRId64 " fail: %s",
            key_id,
            sqlite3_errmsg(config_database)
        );
        return GG_ERR_FAILURE;
    }
    return GG_ERR_OK;
}

static GgError delete_key(int64_t key_id) {
    GG_LOGT("Deleting key id %" PRId64 " from the key table", key_id);
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(config_database, GGL_SQL_DELETE_KEY, -1, &stmt, NULL);
    GG_CLEANUP(cleanup_sqlite3_finalize, stmt);
    sqlite3_bind_int64(stmt, 1, key_id);
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        GG_LOGE(
            "delete key id %" PRId64 " fail: %s",
            key_id,
            sqlite3_errmsg(config_database)
        );
        return GG_ERR_FAILURE;
    }
    return GG_ERR_OK;
}

GgError ggconfig_delete_key(GgList *key_path) {
    if (config_initialized == false) {
        GG_LOGE("Database not initialized.");
        return GG_ERR_FAILURE;
    }

    sqlite3_exec(config_database, "BEGIN TRANSACTION", NULL, NULL, NULL);
    GG_LOGT("Starting transaction to delete key %s", print_key_path(key_path));

    GgObject ids_array[GG_MAX_OBJECT_DEPTH];
    GgObjVec ids = { .list = { .items = ids_array, .len = 0 },
                     .capacity = GG_MAX_OBJECT_DEPTH };
    GgError err = get_key_ids(key_path, &ids);
    if (err == GG_ERR_NOENTRY) {
        sqlite3_exec(config_database, "END TRANSACTION", NULL, NULL, NULL);
        GG_LOGT(
            "Key %s does not exist, nothing to do", print_key_path(key_path)
        );
        return GG_ERR_OK;
    }
    if (err != GG_ERR_OK) {
        sqlite3_exec(config_database, "ROLLBACK", NULL, NULL, NULL);
        return err;
    }
    int64_t key_id = gg_obj_into_i64(ids.list.items[ids.list.len - 1]);

    GgObject descendant_ids_array
        [MAX_CONFIG_DESCENDANTS_PER_COMPONENT]; // Deletes are recursive, so
                                                // worst case, a user is
                                                // resetting their entire
                                                // component configuration
    GgObjVec descendant_ids
        = { .list = { .items = descendant_ids_array, .len = 0 },
            .capacity = MAX_CONFIG_DESCENDANTS_PER_COMPONENT };
    err = get_descendants(key_id, &descendant_ids);
    if (err != GG_ERR_OK) {
        sqlite3_exec(config_database, "ROLLBACK", NULL, NULL, NULL);
        return err;
    }

    for (size_t i = 0; i < descendant_ids.list.len; i++) {
        int64_t descendant_id = gg_obj_into_i64(descendant_ids.list.items[i]);
        err = delete_subscribers(descendant_id);
        if (err != GG_ERR_OK) {
            GG_LOGE(
                "Failed to delete subscribers for id %" PRId64
                " with error %s. This should not happen, but keyids are not reused and thus any subscriptions on this key will not be activated anymore, so execution can continue.",
                descendant_id,
                gg_strerror(err)
            );
        }
        err = delete_value(descendant_id);
        if (err != GG_ERR_OK) {
            sqlite3_exec(config_database, "ROLLBACK", NULL, NULL, NULL);
            return err;
        }
        err = delete_relations(descendant_id);
        if (err != GG_ERR_OK) {
            sqlite3_exec(config_database, "ROLLBACK", NULL, NULL, NULL);
            return err;
        }
        err = delete_key(descendant_id);
        if (err != GG_ERR_OK) {
            sqlite3_exec(config_database, "ROLLBACK", NULL, NULL, NULL);
            return err;
        }
    }

    sqlite3_exec(config_database, "END TRANSACTION", NULL, NULL, NULL);
    return GG_ERR_OK;
}

GgError ggconfig_get_key_notification(GgList *key_path, uint32_t handle) {
    GgError return_err = GG_ERR_FAILURE;

    if (config_initialized == false) {
        GG_LOGE("Database not initialized");
        return GG_ERR_FAILURE;
    }

    sqlite3_exec(config_database, "BEGIN TRANSACTION", NULL, NULL, NULL);
    GG_LOGT(
        "Starting transaction to subscribe to key %s", print_key_path(key_path)
    );

    // ensure this key is present in the key path. Key does not require a
    // value
    GgObject ids_array[GG_MAX_OBJECT_DEPTH];
    GgObjVec ids = { .list = { .items = ids_array, .len = 0 },
                     .capacity = GG_MAX_OBJECT_DEPTH };
    GgError err = get_key_ids(key_path, &ids);
    if (err == GG_ERR_NOENTRY) {
        sqlite3_exec(config_database, "ROLLBACK", NULL, NULL, NULL);
        return GG_ERR_NOENTRY;
    }
    if (err != GG_ERR_OK) {
        sqlite3_exec(config_database, "ROLLBACK", NULL, NULL, NULL);
        return err;
    }
    int64_t key_id = gg_obj_into_i64(ids.list.items[ids.list.len - 1]);

    // insert the key & handle data into the subscriber database
    GG_LOGT("INSERT %" PRId64 ", %" PRIu32, key_id, handle);
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(
        config_database, GGL_SQL_ADD_SUBSCRIPTION, -1, &stmt, NULL
    );
    GG_CLEANUP(cleanup_sqlite3_finalize, stmt);
    sqlite3_bind_int64(stmt, 1, key_id);
    sqlite3_bind_int64(stmt, 2, handle);
    int rc = sqlite3_step(stmt);
    sqlite3_exec(config_database, "END TRANSACTION", NULL, NULL, NULL);
    if (SQLITE_DONE != rc) {
        GG_LOGE("%d %s", rc, sqlite3_errmsg(config_database));
    } else {
        GG_LOGT("Success");
        return_err = GG_ERR_OK;
    }

    return return_err;
}
