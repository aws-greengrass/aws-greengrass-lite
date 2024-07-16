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

/* TODO: do something to ensure the keypath is valid */
bool validate_keys(const char *key) {
    return true;
}

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
                = "create table config("
                  "'path' text not null unique collate nocase,"
                  "'isValue' int default 0,"
                  "'value' text not null default '',"
                  "'parent'  text not null default '' collate nocase,"
                  "primary key(path),"
                  "foreign key(parent) references config(path) );";

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

GglError ggconfig_insert_key_and_value(const char *key, const char *value) {
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

    // strip the parent key
    // backup the character pointer from the previous path check until we reach
    // the last / or the beginning of the string.
    while (*c != '/' && c != key) {
        c--;
    }
    // copy the shorter string to the parentKey variable.
    // rely upon the initialization of 0's in parentKey for null termination.
    assert(c >= key);
    if (c > key) {
        memcpy(parentKey, key, (unsigned long) ((char *) c - (char *) key));
    }
    snprintf(
        sqlQuery,
        sizeof(sqlQuery),
        "INSERT INTO config(path, isValue, value, parent) VALUES ('%s', 1, "
        "'%s', '%s');",
        key,
        value,
        parentKey
    );
    GGL_LOGI("gglconfig_insert", "insert query: %s", sqlQuery);
    int rc = sqlite3_exec(config_database, sqlQuery, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        GGL_LOGE("ggconfig_insert", "%s", errmsg);
        return GGL_ERR_FAILURE;
    }
    return GGL_ERR_OK;
}

GglError ggconfig_get_value_from_key(
    const char *key, char *const value_buffer, int *value_buffer_length
) {
    sqlite3_stmt *stmt;
    GglError returnValue = GGL_ERR_FAILURE;

    if (config_initialized == false) {
        return GGL_ERR_FAILURE;
    }
    if (validate_keys(key)) {
        char sqlQuery[128] = { 0 };
        snprintf(
            sqlQuery,
            sizeof(sqlQuery),
            "SELECT value FROM config WHERE path = '%s';",
            key
        );
        sqlite3_prepare_v2(config_database, sqlQuery, -1, &stmt, NULL);
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
    }
    return returnValue;
}

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
