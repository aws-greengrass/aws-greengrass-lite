#include "ggl/error.h"
#include "ggl/object.h"
#include "stdlib.h"

/*
    The ggconfig_Callback_t will be called with the stored parameter when the key is written.
    The keyvalue can be read with the getValueFromKey() function.
*/
typedef  void GglConfigCallback(void *parameter);

/* TODO: Make const strings into buffers */

GglError ggconfig_insertKeyAndValue( const char *key, const char *value );
GglError ggconfig_getValueFromKey(const char *key, const char *valueBuffer, size_t *valueBufferLength );
GglError ggconfig_getKeyNotification(const char *key, GglConfigCallback callback, void *parameter);
