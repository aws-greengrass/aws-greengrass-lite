#include "ggconfig.h"
#include <assert.h>
#include <ctype.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

static bool config_initialized = false;
static sqlite3 *config_database;
static const char *config_database_name = "config.db";

GglError ggconfig_open(void) {
    if (config_initialized == false) {
        /* do configuration */
        int rc = sqlite3_open(config_database_name, &config_database);
        if (rc) {
            GGL_LOGE(
                "ggconfiglib",
                "Cannot open the configuration database: %s",
                sqlite3_errmsg(config_database)
            );
            return GGL_ERR_FAILURE;
        }
        GGL_LOGI("GGCONFIG", "Config database Opened");

        sqlite3_stmt *stmt;

        sqlite3_prepare_v2(
            config_database,
            "SELECT name FROM sqlite_master WHERE type = 'table' AND name = "
            "'pathTable';",
            -1,
            &stmt,
            NULL
        );
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            GGL_LOGI("ggconfig_open", "found pathtable");
        } else {
            GGL_LOGI("ggconfig_open", "creating the database");
            /* create the initial table */
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
                  "CREATE TRIGGER update_Timestamp_Trigger"
                  "AFTER UPDATE On valueTable BEGIN "
                  "UPDATE valueTable SET timeStamp = CURRENT_TIMESTAMP WHERE "
                  "pathid = NEW.pathid;"
                  "END;";

            result = sqlite3_exec(
                config_database, create_query, NULL, NULL, &err_message
            );
            if (result) {
                if (err_message) {
                    GGL_LOGI("GGCONFIG", "%d %s", result, err_message);
                    sqlite3_free(err_message);
                }
                return GGL_ERR_FAILURE;
            }
        }

        config_initialized = true;
    }
    return GGL_ERR_OK;
}

GglError ggconfig_close(void) {
    sqlite3_close(config_database);
    config_initialized = false;
    return GGL_ERR_OK;
}

static long long path_insert(const char *key) {
    sqlite3_stmt *path_insert_stmt;
    long long id = 0;

    sqlite3_prepare_v2(
        config_database,
        "INSERT INTO pathTable(pathvalue) VALUES (?);",
        -1,
        &path_insert_stmt,
        NULL
    );
    /* insert this element in the root level (as a path not in
     * the relation )*/
    sqlite3_bind_text(path_insert_stmt, 1, key, -1, SQLITE_STATIC);
    sqlite3_step(path_insert_stmt);
    id = sqlite3_last_insert_rowid(config_database);
    GGL_LOGI("ggconfig_insert", "insert %s result: %lld", key, id);
    sqlite3_finalize(path_insert_stmt);
    return id;
}

static bool value_is_present_for_key(const char *key) {
    sqlite3_stmt *find_value_stmt;
    bool return_value = false;
    GGL_LOGI("value present", "checking %s", key);
    sqlite3_prepare_v2(
        config_database,
        "SELECT pathid FROM valueTable where pathid = (SELECT pathid FROM "
        "pathTable WHERE pathValue = ?);",
        -1,
        &find_value_stmt,
        NULL
    );
    sqlite3_bind_text(find_value_stmt, 1, key, -1, SQLITE_STATIC);
    int rc = sqlite3_step(find_value_stmt);
    GGL_LOGI("ggconfig_value", "value is present rc : %d", rc);
    if (rc == SQLITE_ROW) {
        long long pid = sqlite3_column_int(find_value_stmt, 0);
        if (pid) {
            return_value = true;
        }
    }
    return return_value;
}

/* TODO check this query */
static long long find_path_with_parent(const char *key) {
    sqlite3_stmt *find_element_stmt;
    long long id = 0;
    sqlite3_prepare_v2(
        config_database,
        "SELECT pathid FROM pathTable WHERE pathid IN (SELECT pathid FROM "
        "relationTable) AND pathvalue = ?;",
        -1,
        &find_element_stmt,
        NULL
    );
    /* get the ID of this item after the parent */
    sqlite3_bind_text(find_element_stmt, 1, key, -1, SQLITE_STATIC);
    int rc = sqlite3_step(find_element_stmt);
    GGL_LOGI("ggconfig_insert", "find element returned %d", rc);
    if (rc == SQLITE_ROW) {
        id = sqlite3_column_int(find_element_stmt, 0);
        GGL_LOGI("ggconfig_insert", "found %s at %lld", key, id);
    } else {
        GGL_LOGI("ggconfig_insert", "%s not found", key);
    }
    sqlite3_finalize(find_element_stmt);
    return id;
}

static long long get_parent_key_at_root(const char *key) {
    sqlite3_stmt *root_check_stmt;
    long long id = 0;
    int rc = 0;

    /* get a pathid where the path is a root (first element of a path) */
    sqlite3_prepare_v2(
        config_database,
        "select pathid from pathTable where pathid not in (select "
        "pathid from relationTable) and pathvalue = ?;",
        -1,
        &root_check_stmt,
        NULL
    );

    sqlite3_bind_text(root_check_stmt, 1, key, -1, SQLITE_STATIC);
    rc = sqlite3_step(root_check_stmt);
    if (rc == SQLITE_ROW) { /* exists as a root and here is the id */
        id = sqlite3_column_int(root_check_stmt, 0);
        GGL_LOGI("ggconfig_insert", "Found %s at %lld", key, id);
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
        "insert into relationTable(pathid,parentid) values (?,?);",
        -1,
        &relation_insert_stmt,
        NULL
    );
    sqlite3_bind_int64(relation_insert_stmt, 1, id);
    sqlite3_bind_int64(relation_insert_stmt, 2, parent);
    int rc = sqlite3_step(relation_insert_stmt);
    if (rc == SQLITE_DONE || rc == SQLITE_OK) {
        GGL_LOGI(
            "ggconfig_insert",
            "relation insert successful path:%lld, "
            "parent:%lld",
            id,
            parent
        );
    } else {
        GGL_LOGE(
            "ggconfig_insert",
            "relation insert fail: %s",
            sqlite3_errmsg(config_database)
        );
    }
    sqlite3_finalize(relation_insert_stmt);
}

static GglError value_insert(const char *key, const char *value) {
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
    sqlite3_bind_text(value_insert_stmt, 1, key, -1, SQLITE_STATIC);
    sqlite3_bind_text(value_insert_stmt, 2, value, -1, SQLITE_STATIC);
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

static GglError value_update(const char *key, const char *value) {
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
    sqlite3_bind_text(update_value_stmt, 1, value, -1, SQLITE_STATIC);
    sqlite3_bind_text(update_value_stmt, 2, key, -1, SQLITE_STATIC);
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

static bool validate_key(char *key) {
    char *c = key;
    /* Verify that the path is alpha characters or / and nothing else */
    if (!isalpha(*c)) { /* make sure the path starts with a character */
        return false;
    }
    while (*c != 0) { /* make sure the rest of the path is characters or /'s */
        if (!isalpha(*c) && *c != '/') {
            return false;
        }
        c++;
    }
    return true;
}

GglError ggconfig_write_value_at_key(const char *key, const char *value) {
    GglError return_value = GGL_ERR_FAILURE;
    long long id = 0;
    long long parent_id = 0;
    char *c = (char *) key;
    char parent_key[128] = { 0 };
    int depth_count = 0;
    unsigned long path_index = 0;

    if (config_initialized == false) {
        return GGL_ERR_FAILURE;
    }

    if (validate_key((char *) key) == false) {
        return GGL_ERR_INVALID;
    }

    sqlite3_exec(config_database, "BEGIN TRANSACTION", NULL, NULL, NULL);

    c = (char *) key;
    while (*c) {
        assert(path_index < (sizeof(parent_key) / sizeof(*parent_key)));
        if (*c == '/' || *c == 0) {
            if (depth_count == 0) { /* root level of the key path */
                id = get_parent_key_at_root(parent_key);
                if (id == 0) {
                    id = path_insert(parent_key);
                }
            } else { /* all other key path levels */
                id = find_path_with_parent(parent_key);

                /* if this id is not in the path, add it.*/
                if (id == 0) {
                    GGL_LOGI("ggconfig_insert", "inserting %s", parent_key);
                    id = path_insert(parent_key);
                    relation_insert(id, parent_id);
                }
            }
        }
        parent_id = id;
        depth_count++;
        parent_key[path_index++] = *c++;
    }
    id = path_insert(key);
    relation_insert(id, parent_id);
    GGL_LOGI("ggconfig_insert", "time to insert/update %s", key);
    if (value_is_present_for_key(key)) {
        return_value = value_update(key, value);
    } else {
        return_value = value_insert(key, value);
    }

    sqlite3_exec(config_database, "END TRANSACTION", NULL, NULL, NULL);

    GGL_LOGI("ggconfig_insert", "finished with %s", key);

    return return_value;
}

GglError ggconfig_get_value_from_key(
    const char *key, char *value_buffer, int *value_buffer_length
) {
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
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        *value_buffer_length = snprintf(
            value_buffer,
            *value_buffer_length,
            "%s",
            sqlite3_column_text(stmt, 0)
        );
        GGL_LOGI("ggconfig_get", "%s", value_buffer);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            GGL_LOGE("ggconfig_get", "%s", sqlite3_errmsg(config_database));
            return_value = GGL_ERR_FAILURE;
        } else {
            return_value = GGL_ERR_OK;
        }
    }
    sqlite3_finalize(stmt);
    return return_value;
}

/* TODO: implement this */
GglError ggconfig_get_key_notification(
    const char *key, GglConfigCallback callback, void *parameter
) {
    (void) key;
    (void) callback;
    (void) parameter;

    if (config_initialized == false) {
        return GGL_ERR_FAILURE;
    }

    return GGL_ERR_FAILURE;
}
