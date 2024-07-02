#include "../include/ggconfig.h"
#include <sqlite3.h>
#include <stdbool.h>

/* note keypath is a null terminated string. */
static int count_key_path_depth(const char *keypath) {
    int count = 0;
    for (const char *c = keypath; *c != 0; c++) {
        if (*c == '/') {
            count++;
        }
    }
    return count;
}

void make_configuration_ready(void) {
    static bool config_initialized = false;
    if (config_initialized == false) {
        /* do configuration */
    }
}

bool validate_keys(const char *key) {
    return true;
}

GglError ggconfig_insert_key_and_value(const char *key, const char *value) {
    make_configuration_ready();
    /* create a new key on the keypath for this component */
    return GGL_ERR_FAILURE;
}

GglError ggconfig_getValueFromKey(
    const char *key, const char *value_buffer, size_t *value_buffer_length
) {
    make_configuration_ready();
    if (validate_keys(key)) {
        /* collect the data and write it to the supplied buffer. */
        /* if the valueBufferLength is too small, return GGL_ERR_FAILURE */
        return GGL_ERR_OK;
    } else {
        return GGL_ERR_FAILURE;
    }
}

GglError ggconfig_get_key_notification(
    const char *key, GglConfigCallback callback, void *parameter
) {
    make_configuration_ready();
    return GGL_ERR_FAILURE;
}
