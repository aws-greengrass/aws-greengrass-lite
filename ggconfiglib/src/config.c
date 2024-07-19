#include "ggconfig.h"
#include <assert.h>
#include <ctype.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <memory.h>
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
        } else {
            GGL_LOGI("GGLCONFIG", "Config database Opened");

            /* create the initial table */
            int result;
            char *errMessage = 0;

            const char *createQuery
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
                config_database, createQuery, NULL, NULL, &errMessage
            );
            if (result) {
                if (errMessage) {
                    GGL_LOGI("GGLCONFIG", "%s", errMessage);
                    sqlite3_free(errMessage);
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

/* TODO: minimize the number of prepared statements. */
static bool insertWholePath(const char *key, const char *value) {
    long long parent_id = 0;
    char *c = NULL;
    bool return_value = false;
    char parent_key[128] = { 0 };
    sqlite3_stmt *root_check_stmt;
    sqlite3_stmt *path_insert_stmt;
    sqlite3_stmt *relation_insert_stmt;
    sqlite3_stmt *find_element_stmt;
    sqlite3_stmt *value_insert_stmt;
    sqlite3_stmt *path_find_stmt;
    sqlite3_stmt *update_value_stmt;

    /* get a pathid where the path is a root (first element of a path) */
    sqlite3_prepare_v2(
        config_database,
        "select pathid from pathTable where pathid not in (select "
        "pathid from relationTable) and pathvalue = ?;",
        -1,
        &root_check_stmt,
        NULL
    );
    sqlite3_prepare_v2(
        config_database,
        "insert into pathTable(pathvalue) VALUES (?);",
        -1,
        &path_insert_stmt,
        NULL
    );
    sqlite3_prepare_v2(
        config_database,
        "insert into relationTable(pathid,parentid) values (?,?);",
        -1,
        &relation_insert_stmt,
        NULL
    );
    /* get a pathid where the path is a parent */
    sqlite3_prepare_v2(
        config_database,
        "SELECT pathid FROM pathTable WHERE "
        "pathvalue = ?;",
        -1,
        &find_element_stmt,
        NULL
    );
    sqlite3_prepare_v2(
        config_database,
        "INSERT INTO valueTable(pathid,value) VALUES (?,?)",
        -1,
        &value_insert_stmt,
        NULL
    );
    /* get the pathid for a specified path  that is NOT referenced in the value
     * table and is NOT referenced as a parent in the relation table */
    sqlite3_prepare_v2(
        config_database,
        "SELECT pathid FROM pathTable WHERE pathid NOT IN (SELECT pathid from "
        "valueTable) AND pathid NOT IN (SELECT parentid from relationTable) "
        "AND pathvalue = ?;",
        -1,
        &path_find_stmt,
        NULL
    );

    sqlite3_prepare_v2(
        config_database,
        "UPDATE valueTable SET value = ? WHERE pathid = (SELECT pathid from "
        "pathTable where pathvalue = ?);",
        -1,
        &update_value_stmt,
        NULL
    );
    sqlite3_exec(config_database, "BEGIN TRANSACTION", NULL, NULL, NULL);

    int depth_count = 0;
    unsigned long path_index = 0;
    c = (char *) key - 1;
    do {
        c++;
        assert(path_index < (sizeof(parent_key) / sizeof(*parent_key)));
        if (*c == '/' || *c == 0) {
            GGL_LOGI("ggconfig_insert", "working on %s", parent_key);
            if (depth_count == 0) {
                sqlite3_bind_text(
                    root_check_stmt, 1, parent_key, -1, SQLITE_STATIC
                );
                if (sqlite3_step(root_check_stmt)
                    == SQLITE_ROW) { /* exists as a root and here is the id */
                    parent_id = sqlite3_column_int(root_check_stmt, 0);
                    GGL_LOGI(
                        "ggconfig_insert",
                        "Found %s at %lld",
                        parent_key,
                        parent_id
                    );
                } else {
                    /* insert this element in the root level (as a path not in
                     * the relation )*/
                    sqlite3_bind_text(
                        path_insert_stmt, 1, parent_key, -1, SQLITE_STATIC
                    );
                    int rc = sqlite3_step(path_insert_stmt);
                    parent_id = sqlite3_last_insert_rowid(config_database);
                    GGL_LOGI(
                        "ggconfig_insert",
                        "insert %s result %d : %lld",
                        parent_key,
                        rc,
                        parent_id
                    );
                    sqlite3_reset(path_insert_stmt);
                }
                sqlite3_reset(root_check_stmt);
            } else {
                /* get the ID of this item after the parent */
                sqlite3_bind_text(
                    find_element_stmt, 1, parent_key, -1, SQLITE_STATIC
                );
                int rc = sqlite3_step(find_element_stmt);
                GGL_LOGI("ggconfig_insert", "find element returned %d", rc);
                if (rc == SQLITE_ROW) {
                    parent_id = sqlite3_column_int(find_element_stmt, 0);
                    GGL_LOGI(
                        "ggconfig_insert",
                        "found element with parent %s %lld",
                        parent_key,
                        parent_id
                    );

                } else {
                    GGL_LOGI("ggconfig_insert", "inserting %s", parent_key);
                    sqlite3_bind_text(
                        path_insert_stmt, 1, parent_key, -1, NULL
                    );
                    rc = sqlite3_step(path_insert_stmt);
                    if (rc == SQLITE_DONE || rc == SQLITE_OK) {
                        GGL_LOGI(
                            "ggconfig_insert",
                            "insert successful %s",
                            parent_key
                        );
                        long long pathid
                            = sqlite3_last_insert_rowid(config_database);
                        sqlite3_bind_int64(relation_insert_stmt, 1, pathid);
                        sqlite3_bind_int64(relation_insert_stmt, 2, parent_id);
                        rc = sqlite3_step(relation_insert_stmt);
                        if (rc == SQLITE_DONE || rc == SQLITE_OK) {
                            GGL_LOGI(
                                "ggconfig_insert",
                                "relation insert successful path:%lld, "
                                "parent:%lld",
                                pathid,
                                parent_id
                            );
                        } else {
                            GGL_LOGE(
                                "ggconfig_insert",
                                "relation insert fail: %s",
                                sqlite3_errmsg(config_database)
                            );
                        }
                        parent_id = pathid;
                        sqlite3_reset(relation_insert_stmt);
                    } else {
                        GGL_LOGE(
                            "ggconfig_insert",
                            "path insert fail for %s",
                            parent_key
                        );
                    }
                    sqlite3_reset(path_insert_stmt);
                }
                sqlite3_reset(find_element_stmt);
            }
            depth_count++;
            if (*c == 0) {
                int pathid = 0;
                sqlite3_bind_text(path_find_stmt, 1, key, -1, SQLITE_STATIC);
                if (sqlite3_step(path_find_stmt) == SQLITE_ROW) {
                    pathid = sqlite3_column_int(path_find_stmt, 0);

                    sqlite3_bind_int(value_insert_stmt, 1, pathid);
                    sqlite3_bind_text(
                        value_insert_stmt, 2, value, -1, SQLITE_STATIC
                    );
                    int rc = sqlite3_step(value_insert_stmt);
                    if (rc == SQLITE_DONE || rc == SQLITE_OK) {
                        GGL_LOGI("ggconfig_insert", "value insert successful");
                        return_value = true;
                    } else {
                        GGL_LOGE(
                            "ggconfig_insert",
                            "value insert fail : %s",
                            sqlite3_errmsg(config_database)
                        );
                    }
                    sqlite3_reset(value_insert_stmt);
                } else {
                    GGL_LOGI(
                        "ggconfig_insert", "value already present.  updating"
                    );
                    sqlite3_bind_text(
                        update_value_stmt, 1, value, -1, SQLITE_STATIC
                    );
                    sqlite3_bind_text(
                        update_value_stmt, 2, key, -1, SQLITE_STATIC
                    );
                    int rc = sqlite3_step(update_value_stmt);
                    GGL_LOGI("ggconfig_insert", "%d", rc);
                    if (rc == SQLITE_DONE || rc == SQLITE_OK) {
                        GGL_LOGI("ggconfig_insert", "value update successful");
                        return_value = true;
                    } else {
                        GGL_LOGE(
                            "ggconfig_insert",
                            "value update fail : %s",
                            sqlite3_errmsg(config_database)
                        );
                    }
                    sqlite3_reset(update_value_stmt);
                }
                sqlite3_reset(path_find_stmt);
            }
        }
        parent_key[path_index++] = *c;
    } while (*c);
    sqlite3_finalize(find_element_stmt);
    sqlite3_finalize(path_insert_stmt);
    sqlite3_finalize(relation_insert_stmt);
    sqlite3_finalize(root_check_stmt);
    sqlite3_finalize(value_insert_stmt);
    sqlite3_finalize(path_find_stmt);

    sqlite3_exec(config_database, "END TRANSACTION", NULL, NULL, NULL);

    GGL_LOGI("ggconfig_insert", "finished with %s", key);
    return return_value;
}

GglError ggconfig_write_value_at_key(const char *key, const char *value) {
    if (config_initialized == false) {
        return GGL_ERR_FAILURE;
    }
    /* Verify that the path is alpha characters or / and nothing else */
    char *c = (char *) key;

    if (!isalpha(*c)) { /* make sure the path starts with a character */
        return GGL_ERR_INVALID;
    }
    while (*c != 0) { /* make sure the rest of the path is characters or /'s */
        if (!isalpha(*c) && *c != '/') {
            return GGL_ERR_INVALID;
        }
        c++;
    }
    GGL_LOGI("ggconfig_insert", "Inserting %s: %s", key, value);

    if (insertWholePath(key, value)) {
        return GGL_ERR_OK;
    }
    return GGL_ERR_FAILURE;
}

GglError ggconfig_get_value_from_key(
    const char *key, char *const value_buffer, int *value_buffer_length
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
