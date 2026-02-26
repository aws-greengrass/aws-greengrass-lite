// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/log.h>
#include <gg/types.h>
#include <ggipc/auth.h>
#include <string.h>
#include <sys/types.h>
#include <systemd/sd-login.h>

GgError ggl_ipc_auth_validate_name(pid_t pid, GgBuffer component_name) {
    char *unit_name = NULL;
    int error = sd_pid_get_unit(pid, &unit_name);
    GG_CLEANUP(cleanup_free, unit_name);
    if ((error < 0) || (unit_name == NULL)) {
        GG_LOGE("Failed to look up service for pid %d.", pid);
        return GG_ERR_FAILURE;
    }

    GgBuffer name = gg_buffer_from_null_term(unit_name);

    if (!gg_buffer_remove_suffix(&name, GG_STR(".service"))) {
        GG_LOGE(
            "Service for pid %d (%s) missing service extension.", pid, unit_name
        );
        return GG_ERR_FAILURE;
    }

    (void) (gg_buffer_remove_suffix(&name, GG_STR(".install"))
            || gg_buffer_remove_suffix(&name, GG_STR(".bootstrap")));

    if (!gg_buffer_remove_prefix(&name, GG_STR("ggl."))) {
        GG_LOGE(
            "Service for pid %d (%s) does not have ggl component prefix.",
            pid,
            unit_name
        );
        return GG_ERR_FAILURE;
    }

    if (!gg_buffer_eq(name, component_name)) {
        GG_LOGE(
            "Client claims to be %.*s, found to be %.*s instead.",
            (int) component_name.len,
            component_name.data,
            (int) name.len,
            name.data
        );
        return GG_ERR_FAILURE;
    }

    return GG_ERR_OK;
}
