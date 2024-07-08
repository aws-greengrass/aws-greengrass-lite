#include "ggconfig.h"
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
        }
    }
    return GGL_ERR_OK;
}

GglError ggconfig_close(void) {
    sqlite3_close(config_database);
    return GGL_ERR_OK;
}

static bool validate_keys(const char *key) {
    (void) count_key_path_depth(key);
    GGL_LOGI("GGLCONFIG", "validate keypath");
    return true;
}

GglError ggconfig_insert_key_and_value(const char *key, const char *value) {
    (void) key;
    (void) value;
    if (config_initialized == false) {
        return GGL_ERR_FAILURE;
    }
    /* create a new key on the keypath for this component */
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
