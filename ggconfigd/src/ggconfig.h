#include "ggl/error.h"
#include "ggl/object.h"
#include "stdlib.h"

#define MAX_COMPONENT_SIZE 1024
#define MAX_KEY_SIZE 1024
#define MAX_VALUE_SIZE 1024

/// The ggconfig_Callback_t will be called with the stored parameter when the
/// key is written. The keyvalue can be read with the getValueFromKey()
/// function.
typedef void GglConfigCallback(void *parameter);

GglError ggconfig_write_value_at_key(GglBuffer *key, GglBuffer *value);

GglError ggconfig_get_value_from_key(GglBuffer *key, GglBuffer *value_buffer);
GglError ggconfig_get_key_notification(GglBuffer *key, uint32_t handle);
GglError ggconfig_open(void);
GglError ggconfig_close(void);

void ggconfigd_start_server(void);
