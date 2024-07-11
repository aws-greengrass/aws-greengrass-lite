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
    for (const char *c = keypath; *c != 0; c++) {
        if (*c == '/') {
            count++;
        }
    }
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
            const char *createQuery
                = "create table config('parentid' integer primary key, "
                  "'key' string,"
                  "'value' string,"
                  "UNIQUE(rowid),"
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
    for (int keyIndex = 0; keyIndex < keyPathDepth; keyIndex++) {
        char aKey[32] = { 0 };
        int i = 0;
        int keyID = 0;
        /* extract the key */
        if (*c == '/') c++;
        while (*c != 0 && *c != '/' && i < sizeof(aKey)) {
            aKey[i] = *c;
            c++;
            i++;
        }
        if (*c == 0) {
            /* we are at the last key.*/
            GGL_LOGI("insert", "last key %s", aKey);
        } else {
            sqlite3_stmt *stmt;
            char *errmsg;
            char searchString[128] = { 0 };
            GGL_LOGI("insert", "processing %s", aKey);
            snprintf(
                searchString,
                sizeof(searchString),
                "SELECT rowid from config where parentid is NULL and key = "
                "%s;",
                aKey
            );
            int rc = sqlite3_prepare_v2(
                config_database, searchString, -1, &stmt, NULL
            );
            if (rc != SQLITE_OK) {
                GGL_LOGE("insert", "%s", sqlite3_errmsg(config_database));
            } else {
                while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
                    int id = sqlite3_column_int(stmt, 0);
                    const char *name = sqlite3_column_text(stmt, 1);
                    printf("%d %s\n", id, name);
                }
                if (rc != SQLITE_DONE) {
                    GGL_LOGE(
                        "insert", "error %s", sqlite3_errmsg(config_database)
                    );
                }
            }
            sqlite3_finalize(stmt);
        }
    }
    /* Next insert insert the remaining keys to form the key path */
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
