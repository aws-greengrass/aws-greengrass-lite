#include "ggconfig.h"
#include <sqlite3.h>
#include <stdbool.h>

static bool configInitialized = false;

/* note keypath is a null terminated string. */
static int countKeyPathDepth(const char *keypath) {
    int count = 0;
    for (char *c = keypath; *c != 0; c++) {
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

GglError ggconfig_insertKeyAndValue(const char *key, const char *value) {
    makeConfigurationReady();
    /* create a new key on the keypath for this component */
    if () {
    } else {
        return GGL_ERR_FAILURE;
    }
}

GglError ggconfig_getValueFromKey(
    const char *key,
    const char *valueBuffer,
    size_t *valueBufferLength,
    const char *component
) {
    makeConfigurationReady();
    if (validateKeys(key)) {
        /* collect the data and write it to the supplied buffer. */
        /* if the valueBufferLength is too small, return GGL_ERR_FAILURE */
    } else {
        return GGL_ERR_FAILURE;
    }
}

GglError ggconfig_insertComponent(const char *component) {
    makeConfigurationReady();
    if () {
        return GGL_ERR_FAILURE;
    } else {
        /* create the new component */
    }
}

GglError ggconfig_deleteComponent(const char *component) {
    makeConfigurationReady();
    if (isKnownComponent(component)) {
        /* delete the component */
    } else {
        return GGL_ERR_FAILURE;
    }
}

GglError ggconfig_getKeyNotification(
    const char *key,
    const char *component,
    ggconfig_Callback_t callback,
    void *parameter
) {
    makeConfigurationReady();
}
