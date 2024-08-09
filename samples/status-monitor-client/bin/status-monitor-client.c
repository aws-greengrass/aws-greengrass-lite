#include <errno.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/utils.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int main(void) {
    while (true) {
        static uint8_t buffer[10 * sizeof(GglObject)] = { 0 };
        GglBumpAlloc alloc = ggl_bump_alloc_init(GGL_BUF(buffer));

        GglObject result = GGL_OBJ_NULL();
        GglError method_error = GGL_ERR_OK;
        GglError call_error = ggl_call(
            GGL_STR("/aws/ggl/gghealthd"),
            GGL_STR("get_status"),
            GGL_MAP({ GGL_STR("component_name"), GGL_OBJ_STR("gghealthd") }),
            &method_error,
            &alloc.alloc,
            &result
        );
        if (call_error != GGL_ERR_OK) {
            return (int) call_error;
        }
        if (method_error != GGL_ERR_OK) {
            return (int) method_error;
        }
        if (result.type != GGL_TYPE_MAP) {
            return EPROTO;
        }

        GglObject *component_name = NULL;
        bool found = ggl_map_get(
            result.map, GGL_STR("component_name"), &component_name
        );
        if (!found || (component_name->type != GGL_TYPE_BUF)) {
            return EPROTO;
        }

        GglObject *lifecycle_state = NULL;
        found = ggl_map_get(
            result.map, GGL_STR("lifecycle_state"), &lifecycle_state
        );
        if (!found || (lifecycle_state->type != GGL_TYPE_BUF)) {
            return EPROTO;
        }

        GGL_LOGI(
            "status-monitor",
            "%.*s state: %.*s",
            (int) component_name->buf.len,
            component_name->buf.data,
            (int) lifecycle_state->buf.len,
            lifecycle_state->buf.data
        );

        ggl_sleep(1);
    }
}
