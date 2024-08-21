#include "ggl/error.h"
#include "ggl/object.h"
#include "stdlib.h"

// TODO: Pull this value from GGL_COREBUS_MAX_MSG_LEN
#define GGCONFIGD_MAX_DB_READ_BYTES 10000
// TODO: we could save this static memory by having json decoding done as we
// read each object in the db_interface layer.
// For now, set to something slightly smaller than GGCONFIGD_MAX_DB_READ_BYTES
#define GGCONFIGD_MAX_OBJECT_DECODE_BYTES 9000

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
