#include "ggl/error.h"
#include "ggl/object.h"
#include "stdlib.h"

/*
    The ggconfig_Callback_t will be called with the stored parameter when the
   key is written. The keyvalue can be read with the getValueFromKey() function.
*/
typedef void GglConfigCallback(void *parameter);

/* TODO: Make const strings into buffers */

GglError ggconfig_insert_key_and_value(const char *key, const char *value);

GglError ggconfig_get_value_from_key(
    const char *key, const char *value_buffer, size_t *value_buffer_length
);

GglError ggconfig_get_key_notification(
    const char *key, GglConfigCallback callback, void *parameter
);