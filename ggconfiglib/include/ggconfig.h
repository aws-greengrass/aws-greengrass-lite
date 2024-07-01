#include "ggl/error.h"
#include "stdlib.h"

/*
    The ggconfig_Callback_t will be called with the stored parameter when the key is written.
    The keyvalue can be read with the getValueFromKey() function.
*/
typedef  void ggconfig_Callback_t(void *parameter);


GglError ggconfig_writeValueToKey(const char *key, const char *value, const char *component);
GglError ggconfig_insertKeyAndValue(const char *key, const char *value, const char *component);
GglError ggconfig_getValueFromKey(const char *key, const char *valueBuffer, size_t *valueBufferLength, const char *component );
GglError ggconfig_insertComponent(const char *component);
GglError ggconfig_deleteComponent(const char *component);
GglError ggconfig_getKeyNotification(const char *key, const char *component, ggconfig_Callback_t callback, void *parameter);
