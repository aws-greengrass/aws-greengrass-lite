#include "ggconfig.h"
#include <sqlite3.h>
#include <stdbool.h>

static bool configInitialized = false;

/* note keypath is a null terminated string. */
static int countKeyPathDepth(const char *keypath) {
    int count = 0;
    for (const char *c = keypath; *c != 0; c++) {
        if (*c == '/') {
            count++;
        }
    }
    return count;
}

void makeConfigurationReady(void) {
    if (configInitialized = false) {
        /* do configuration */
    }
}

bool validateKeys(const char *key) {
    return true;
}

GglError ggconfig_insertKeyAndValue(const char *key, const char *value) {
    makeConfigurationReady();
    /* create a new key on the keypath for this component */
    if (1) {
    } else {
        return GGL_ERR_FAILURE;
    }
}

GglError ggconfig_getValueFromKey(
    const char *key, const char *valueBuffer, size_t *valueBufferLength
) {
    makeConfigurationReady();
    if (validateKeys(key)) {
        /* collect the data and write it to the supplied buffer. */
        /* if the valueBufferLength is too small, return GGL_ERR_FAILURE */
    } else {
        return GGL_ERR_FAILURE;
    }
}

GglError ggconfig_getKeyNotification(
    const char *key, GglConfigCallback callback, void *parameter
) {
    makeConfigurationReady();
}
