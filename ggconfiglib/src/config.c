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

            if ((result = sqlite3_exec(
                     config_database, createQuery, NULL, NULL, &errMessage
                 ))) {
                if (errMessage) {
                    GGL_LOGI("GGLCONFIG", "%s", errMessage);
                    sqlite3_free(errMessage);
                }
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
    long long parentid = 0;
    char *c = NULL;
    bool returnValue = false;
    char parentKey[128] = { 0 };
    sqlite3_stmt *rootCheckStmt;
    sqlite3_stmt *pathInsertStmt;
    sqlite3_stmt *relationInsertStmt;
    sqlite3_stmt *findElementStmt;
    sqlite3_stmt *valueInsertStmt;
    sqlite3_stmt *pathFindStmt;
    sqlite3_stmt *updateValueStmt;

    /* get a pathid where the path is a root (first element of a path) */
    sqlite3_prepare_v2(
        config_database,
        "select pathid from pathTable where pathid not in (select "
        "pathid from relationTable) and pathvalue = ?;",
        -1,
        &rootCheckStmt,
        NULL
    );
    sqlite3_prepare_v2(
        config_database,
        "insert into pathTable(pathvalue) VALUES (?);",
        -1,
        &pathInsertStmt,
        NULL
    );
    sqlite3_prepare_v2(
        config_database,
        "insert into relationTable(pathid,parentid) values (?,?);",
        -1,
        &relationInsertStmt,
        NULL
    );
    /* get a pathid where the path is a parent */
    sqlite3_prepare_v2(
        config_database,
        "SELECT pathid FROM pathTable WHERE "
        "pathvalue = ?;",
        -1,
        &findElementStmt,
        NULL
    );
    sqlite3_prepare_v2(
        config_database,
        "INSERT INTO valueTable(pathid,value) VALUES (?,?)",
        -1,
        &valueInsertStmt,
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
        &pathFindStmt,
        NULL
    );

    sqlite3_prepare_v2(
        config_database,
        "UPDATE valueTable SET value = ? WHERE pathid = (SELECT pathid from "
        "pathTable where pathvalue = ?);",
        -1,
        &updateValueStmt,
        NULL
    );
    sqlite3_exec(config_database, "BEGIN TRANSACTION", NULL, NULL, NULL);

    int depthCount = 0;
    int pindex = 0;
    c = (char *) key - 1;
    do {
        c++;
        assert(pindex < (sizeof(parentKey) / sizeof(*parentKey)));
        if (*c == '/' || *c == 0) {
            GGL_LOGI("ggconfig_insert", "working on %s", parentKey);
            if (depthCount == 0) {
                sqlite3_bind_text(
                    rootCheckStmt, 1, parentKey, -1, SQLITE_STATIC
                );
                if (sqlite3_step(rootCheckStmt)
                    == SQLITE_ROW) { /* exists as a root and here is the id */
                    parentid = sqlite3_column_int(rootCheckStmt, 0);
                    GGL_LOGI(
                        "ggconfig_insert",
                        "Found %s at %lld",
                        parentKey,
                        parentid
                    );
                } else {
                    /* insert this element in the root level (as a path not in
                     * the relation )*/
                    sqlite3_bind_text(
                        pathInsertStmt, 1, parentKey, -1, SQLITE_STATIC
                    );
                    int rc = sqlite3_step(pathInsertStmt);
                    parentid = sqlite3_last_insert_rowid(config_database);
                    GGL_LOGI(
                        "ggconfig_insert",
                        "insert %s result %d : %lld",
                        parentKey,
                        rc,
                        parentid
                    );
                    sqlite3_reset(pathInsertStmt);
                }
                sqlite3_reset(rootCheckStmt);
            } else {
                /* get the ID of this item after the parent */
                sqlite3_bind_text(
                    findElementStmt, 1, parentKey, -1, SQLITE_STATIC
                );
                int rc = sqlite3_step(findElementStmt);
                GGL_LOGI("ggconfig_insert", "find element returned %d", rc);
                if (rc == SQLITE_ROW) {
                    parentid = sqlite3_column_int(findElementStmt, 0);
                    GGL_LOGI(
                        "ggconfig_insert",
                        "found element with parent %s %lld",
                        parentKey,
                        parentid
                    );

                } else {
                    GGL_LOGI("ggconfig_insert", "inserting %s", parentKey);
                    sqlite3_bind_text(pathInsertStmt, 1, parentKey, -1, NULL);
                    int rc = sqlite3_step(pathInsertStmt);
                    if (rc == SQLITE_DONE || rc == SQLITE_OK) {
                        GGL_LOGI(
                            "ggconfig_insert", "insert successful %s", parentKey
                        );
                        long long pathid
                            = sqlite3_last_insert_rowid(config_database);
                        sqlite3_bind_int64(relationInsertStmt, 1, pathid);
                        sqlite3_bind_int64(relationInsertStmt, 2, parentid);
                        rc = sqlite3_step(relationInsertStmt);
                        if (rc == SQLITE_DONE || rc == SQLITE_OK) {
                            GGL_LOGI(
                                "ggconfig_insert",
                                "relation insert successful path:%lld, "
                                "parent:%lld",
                                pathid,
                                parentid
                            );
                        } else {
                            GGL_LOGE(
                                "ggconfig_insert",
                                "relation insert fail: %s",
                                sqlite3_errmsg(config_database)
                            );
                        }
                        parentid = pathid;
                        sqlite3_reset(relationInsertStmt);
                    } else {
                        GGL_LOGE(
                            "ggconfig_insert",
                            "path insert fail for %s",
                            parentKey
                        );
                    }
                    sqlite3_reset(pathInsertStmt);
                }
                sqlite3_reset(findElementStmt);
            }
            depthCount++;
            if (*c == 0) {
                int pathid = 0;
                sqlite3_bind_text(pathFindStmt, 1, key, -1, SQLITE_STATIC);
                if (sqlite3_step(pathFindStmt) == SQLITE_ROW) {
                    pathid = sqlite3_column_int(pathFindStmt, 0);

                    sqlite3_bind_int(valueInsertStmt, 1, pathid);
                    sqlite3_bind_text(
                        valueInsertStmt, 2, value, -1, SQLITE_STATIC
                    );
                    int rc = sqlite3_step(valueInsertStmt);
                    if (rc == SQLITE_DONE || rc == SQLITE_OK) {
                        GGL_LOGI("ggconfig_insert", "value insert successful");
                        returnValue = true;
                    } else {
                        GGL_LOGE(
                            "ggconfig_insert",
                            "value insert fail : %s",
                            sqlite3_errmsg(config_database)
                        );
                    }
                    sqlite3_reset(valueInsertStmt);
                } else {
                    GGL_LOGI(
                        "ggconfig_insert", "value already present.  updating"
                    );
                    sqlite3_bind_text(
                        updateValueStmt, 1, value, -1, SQLITE_STATIC
                    );
                    sqlite3_bind_text(
                        updateValueStmt, 2, key, -1, SQLITE_STATIC
                    );
                    int rc = sqlite3_step(updateValueStmt);
                    GGL_LOGI("ggconfig_insert", "%d", rc);
                    if (rc == SQLITE_DONE || rc == SQLITE_OK) {
                        GGL_LOGI("ggconfig_insert", "value update successful");
                        returnValue = true;
                    } else {
                        GGL_LOGE(
                            "ggconfig_insert",
                            "value update fail : %s",
                            sqlite3_errmsg(config_database)
                        );
                    }
                    sqlite3_reset(updateValueStmt);
                }
                sqlite3_reset(pathFindStmt);
            }
        }
        parentKey[pindex++] = *c;
    } while (*c);
    sqlite3_finalize(findElementStmt);
    sqlite3_finalize(pathInsertStmt);
    sqlite3_finalize(relationInsertStmt);
    sqlite3_finalize(rootCheckStmt);
    sqlite3_finalize(valueInsertStmt);
    sqlite3_finalize(pathFindStmt);

    sqlite3_exec(config_database, "END TRANSACTION", NULL, NULL, NULL);

    GGL_LOGI("ggconfig_insert", "finished with %s", key);
    return returnValue;
}

GglError ggconfig_write_value_at_key(const char *key, const char *value) {
    int rc;
    int pathid = 0;
    char *errmsg;
    char sqlQuery[256] = { 0 };
    char parentKey[128] = { 0 }; // todo... optimize these string sizes.

    bool returnValue = false;

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
    GglError returnValue = GGL_ERR_FAILURE;

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
            returnValue = GGL_ERR_FAILURE;
        } else {
            returnValue = GGL_ERR_OK;
        }
    }
    sqlite3_finalize(stmt);
    return returnValue;
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
