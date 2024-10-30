// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggipc/auth.h"
#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/cleanup.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <string.h>
#include <systemd/sd-login.h>
#include <stdint.h>

GglError ggl_ipc_auth_lookup_name(
    pid_t pid, GglAlloc *alloc, GglBuffer *component_name
) {
    char *unit_name = NULL;
    int error = sd_pid_get_unit(pid, &unit_name);
    GGL_CLEANUP(cleanup_free, unit_name);
    if ((error < 0) || (unit_name == NULL)) {
        GGL_LOGE("Failed to look up service for pid %d.", pid);
        return GGL_ERR_NOENTRY;
    }

    GglBuffer name
        = { .data = (uint8_t *) unit_name, .len = strlen(unit_name) };

    GglBuffer ext = GGL_STR(".service");
    if ((name.len <= ext.len)
        || !ggl_buffer_eq(
            ggl_buffer_substr(name, name.len - ext.len, SIZE_MAX), ext
        )) {
        GGL_LOGE(
            "Service for pid %d (%s) missing service extension.", pid, unit_name
        );
        return GGL_ERR_NOENTRY;
    }

    name = ggl_buffer_substr(name, 0, name.len - ext.len);

    GglBuffer prefix = GGL_STR("ggl.");
    if (!ggl_buffer_eq(ggl_buffer_substr(name, 0, prefix.len), prefix)) {
        GGL_LOGE(
            "Service for pid %d (%s) does not have ggl component prefix.",
            pid,
            unit_name
        );
        return GGL_ERR_NOENTRY;
    }

    name = ggl_buffer_substr(name, prefix.len, SIZE_MAX);

    uint8_t *component_name_buf = GGL_ALLOCN(alloc, uint8_t, name.len);
    if (component_name_buf == NULL) {
        GGL_LOGE("Component name %.*s is too long.", (int) name.len, name.data);
        return GGL_ERR_NOMEM;
    }

    memcpy(component_name_buf, name.data, name.len);
    *component_name
        = (GglBuffer) { .data = component_name_buf, .len = name.len };
    return GGL_ERR_OK;
}
