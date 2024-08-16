#include "ggl/error.h"
#include "ggl/object.h"
#include "stdlib.h"

#define GGCONFIGD_MAX_COMPONENT_SIZE 1024
#define GGCONFIGD_MAX_KEY_SIZE 1024
#define GGCONFIGD_MAX_VALUE_SIZE 1024

// "PER_REQUEST" means the maximum number of keys or values that can be
// returned in a single read request. This is not the maximum number of
// things that can be stored in the system.
#define GGCONFIGD_MAX_KVS_PER_MAP_PER_REQUEST 128
#define GGCONFIGD_MAX_MAPS_PER_REQUEST 256
#define GGCONFIGD_MAX_KEYS_PER_REQUEST 512
#define GGCONFIGD_MAX_VALUES_PER_REQUEST 256

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
