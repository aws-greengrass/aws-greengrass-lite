// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggipc/auth.h"
#include <ggl/buffer.h>
#include <ggl/cleanup.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <string.h>
#include <systemd/sd-login.h>

GglError ggl_ipc_auth_validate_name(pid_t pid, GglBuffer component_name) {
    char *unit_name = NULL;
    int error = sd_pid_get_unit(pid, &unit_name);
    GGL_CLEANUP(cleanup_free, unit_name);
    if ((error < 0) || (unit_name == NULL)) {
        GGL_LOGE("Failed to look up service for pid %d.", pid);
        return GGL_ERR_FAILURE;
    }

    GglBuffer name = ggl_buffer_from_null_term(unit_name);

    if (!ggl_buffer_remove_suffix(&name, GGL_STR(".service"))) {
        GGL_LOGE(
            "Service for pid %d (%s) missing service extension.", pid, unit_name
        );
        return GGL_ERR_FAILURE;
    }

    (void) (ggl_buffer_remove_suffix(&name, GGL_STR(".install"))
            || ggl_buffer_remove_suffix(&name, GGL_STR(".bootstrap")));

    if (!ggl_buffer_remove_prefix(&name, GGL_STR("ggl."))) {
        GGL_LOGE(
            "Service for pid %d (%s) does not have ggl component prefix.",
            pid,
            unit_name
        );
        return GGL_ERR_FAILURE;
    }

    if (!ggl_buffer_eq(name, component_name)) {
        GGL_LOGE(
            "Client claims to be %.*s, found to be %.*s instead.",
            (int) component_name.len,
            component_name.data,
            (int) name.len,
            name.data
        );
        return GGL_ERR_FAILURE;
    }

    return GGL_ERR_OK;
}
