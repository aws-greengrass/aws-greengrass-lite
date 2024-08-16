#include "ggl/error.h"
#include "ggl/object.h"
#include "stdlib.h"

#define GGCONFIGD_MAX_COMPONENT_SIZE 1024
#define GGCONFIGD_MAX_KEY_SIZE 1024
#define GGCONFIGD_MAX_VALUE_SIZE 1024

/// The ggconfig_Callback_t will be called with the stored parameter when the
/// key is written. The keyvalue can be read with the getValueFromKey()
/// function.
typedef void GglConfigCallback(void *parameter);

GglError ggconfig_write_value_at_key(GglList *key_path, GglBuffer *value);
GglError ggconfig_get_value_from_key(GglList *key_path, GglObject *value);
GglError ggconfig_get_key_notification(GglList *key_path, uint32_t handle);
GglError ggconfig_open(void);
GglError ggconfig_close(void);

void ggconfigd_start_server(void);
