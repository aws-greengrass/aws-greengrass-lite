#include "ggconfig.h"
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

/* note keypath is a null terminated string. */
static int count_key_path_depth(const char *keypath) {
    int count = 0;
    const char *c = keypath;
    bool aWord = false;
    do {
        if (*c == 0 || *c == '/') {
            if (aWord) {
                aWord = false;
                count++;
            }
        } else {
            aWord = true;
        }
    } while (*c++);
    GGL_LOGI("GGLCONFIG", "keypath depth is %d", count);
    return count;
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
            const char *createQuery = "create table config('parentid' integer, "
                                      "'key' string,"
                                      "'value' string,"
                                      "UNIQUE(key,parentid));";
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

static bool validate_keys(const char *key) {
    (void) count_key_path_depth(key);
    GGL_LOGI("GGLCONFIG", "validate keypath");
    return true;
}

/** check for a key with the indicated parent.  Return the rowid for the key if
 * present.  return 0 if no key is found.  Note: rowid's start with 1 so 0 is
 * invalid.  If more than one key is found, return -1. */
static int getKeyId(const char *key, int parent) {
    sqlite3_stmt *stmt;
    char *errmsg;
    char sqlQuery[128] = { 0 };
    int returnValue = 0;

    if (parent == 0) {
        snprintf(
            sqlQuery,
            sizeof(sqlQuery),
            "SELECT rowid from config where parentid is NULL and key = '%s';",
            key
        );
    } else {
        snprintf(
            sqlQuery,
            sizeof(sqlQuery),
            "SELECT rowid from config where parentid = %d and key = '%s';",
            parent,
            key
        );
    }

    int rc = sqlite3_prepare_v2(config_database, sqlQuery, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        GGL_LOGE("ggconfig_insert", "%s", sqlite3_errmsg(config_database));
    } else {
        GGL_LOGI("gglconfig_insert", "found the rows");
        if ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            /* get the rowid out of the column data */
            int cnt = sqlite3_column_count(stmt);
            int id = sqlite3_column_int(stmt, 0);
            const char *name = sqlite3_column_text(stmt, 0);
            GGL_LOGI("gglconfig_insert", "%d %d %s", cnt, id, name);

            if ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
                GGL_LOGE(
                    "gglconfig_insert",
                    "more than one instance of %s with parent %d",
                    key,
                    parent
                );
                returnValue = -1;
            } else {
                returnValue = id;
            }
        }
        if (rc != SQLITE_DONE) {
            GGL_LOGE("insert", "error %s", sqlite3_errmsg(config_database));
            returnValue = -1;
        }
    }
    sqlite3_finalize(stmt);
    return returnValue;
}

static bool insertKey(const char *key, int parent) {
    char *errmsg;
    char sqlQuery[128] = { 0 };
    bool returnValue = false;
    if (parent == 0) {
        snprintf(
            sqlQuery,
            sizeof(sqlQuery),
            "INSERT INTO config(key) VALUES ('%s');",
            key
        );
        GGL_LOGI(
            "gglconfig_insert",
            "setting up for a key only insert without a parent: %s",
            sqlQuery
        );
    } else {
        snprintf(
            sqlQuery,
            sizeof(sqlQuery),
            "INSERT INTO config(parentid, key) VALUES (%d, '%s');",
            parent,
            key
        );
        GGL_LOGI(
            "gglconfig_insert",
            "setting up for a key only insert with a parent: %s",
            sqlQuery
        );
    }
    int rc = sqlite3_exec(config_database, sqlQuery, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        GGL_LOGE("ggconfig_insert", "%s", errmsg);
    } else {
        returnValue = true;
    }
    return returnValue;
}

static bool insertKeyAndValue(const char *key, const char *value, int parent) {
    char *errmsg;
    char sqlQuery[128] = { 0 };
    bool returnValue = false;

    if (parent == 0) {
        snprintf(
            sqlQuery,
            sizeof(sqlQuery),
            "INSERT INTO config(key, value) VALUES ('%s', '%s');",
            key,
            value
        );
        GGL_LOGI(
            "gglconfig_insert",
            "setting up for a key only insert without a parent: %s",
            sqlQuery
        );
    } else {
        snprintf(
            sqlQuery,
            sizeof(sqlQuery),
            "INSERT INTO config(parentid, key, value) VALUES (%d, '%s', '%s');",
            parent,
            key,
            value
        );
        GGL_LOGI(
            "gglconfig_insert",
            "setting up for a key only insert with a parent: %s",
            sqlQuery
        );
    }
    int rc = sqlite3_exec(config_database, sqlQuery, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        GGL_LOGE("ggconfig_insert", "%s", errmsg);
    } else {
        returnValue = true;
    }
    return returnValue;
}

GglError ggconfig_insert_key_and_value(const char *key, const char *value) {
    (void) value;
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
    /* create a new key on the keypath for this component */
    /* first find an existing key with the deepest depth */
    int keyPathDepth = count_key_path_depth(key);
    c = (char *) key;
    int previousID = 0;
    int keyIndex;
    bool searching = true;
    for (keyIndex = 0; keyIndex < keyPathDepth; keyIndex++) {
        size_t i = 0;
        char aKey[32] = { 0 };
        /* extract the key */
        while (*c == '/') {
            c++;
        }
        while (*c != 0 && *c != '/' && i < sizeof(aKey)) {
            aKey[i] = *c;
            c++;
            i++;
        }
        
        if (searching) {
            GGL_LOGI("gglconfig_insert", "searching %s", aKey);
            if (keyIndex == 0) {
                previousID = getKeyId(aKey, 0);
            } else {
                previousID = getKeyId(aKey, previousID);
            }
            if (previousID < 0) {
                return GGL_ERR_FAILURE;
            }
            if (previousID == 0) {
                searching = false;
            }
        }
        if (!searching) {
            GGL_LOGI("gglconfig_insert", "inserting %s", aKey);
            if (keyIndex != keyPathDepth - 1) {
                insertKey(aKey, previousID);
                previousID = sqlite3_last_insert_rowid(config_database);
            } else {
                insertKeyAndValue(aKey, value, previousID);
                previousID = sqlite3_last_insert_rowid(config_database);
            }
        }
    }
    /* finally add the new key & value */
    return GGL_ERR_OK;
}

GglError ggconfig_get_value_from_key(
    const char *key, const char *value_buffer, size_t *value_buffer_length
) {
    (void) value_buffer;
    *value_buffer_length = 0;

    if (config_initialized == false) {
        return GGL_ERR_FAILURE;
    }
    if (validate_keys(key)) {
        /* collect the data and write it to the supplied buffer. */
        /* if the valueBufferLength is too small, return GGL_ERR_FAILURE */
        return GGL_ERR_OK;
    }
    return GGL_ERR_FAILURE;
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
