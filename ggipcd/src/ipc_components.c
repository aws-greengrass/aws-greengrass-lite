// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ipc_components.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <ggipc/auth.h>
#include <ggl/alloc.h>
#include <ggl/base64.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/socket.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef GGL_IPC_AUTH_DISABLE
#warning "INSECURE!!! IPC authentication disabled!"

__attribute__((constructor)) static void warn_auth_disabled(void) {
    GGL_LOGE("ggipcd", "INSECURE!!! IPC authentication disabled!");
    GGL_LOGE("ggipcd", "SVCUID handling is in debug mode.");
}
#endif

#ifndef GGL_IPC_AUTH_DISABLE
static const bool AUTH_ENABLED = true;
#else
static const bool AUTH_ENABLED = false;
#endif

/// Maximum length of generic component name.
#define MAX_COMPONENT_NAME_LENGTH (128)

static_assert(
    GGL_IPC_SVCUID_LEN % 4 == 0, "GGL_IPC_SVCUID_LEN must be a multiple of 4."
);

#define SVCUID_BIN_LEN (((size_t) GGL_IPC_SVCUID_LEN / 4) * 3)

static uint8_t svcuids[GGL_MAX_GENERIC_COMPONENTS][SVCUID_BIN_LEN];
static uint8_t component_names[GGL_MAX_GENERIC_COMPONENTS]
                              [MAX_COMPONENT_NAME_LENGTH];
static uint8_t component_name_lengths[GGL_MAX_GENERIC_COMPONENTS];

static GglComponentHandle registered_components = 0;

static int random_fd;

__attribute__((constructor)) static void init_urandom_fd(void) {
    random_fd = open("/dev/random", O_RDONLY);
    if (random_fd == -1) {
        int err = errno;
        GGL_LOGE("ipc-server", "Failed to open /dev/random: %d.", err);
        // exit() is not re-entrant and this is safe as long as no spawned
        // thread can call exit()
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        exit(-1);
    }
}

GglBuffer ggl_ipc_components_get_name(GglComponentHandle component_handle) {
    assert(component_handle != 0);
    assert(component_handle <= registered_components);
    return (GglBuffer) { .data = component_names[component_handle - 1],
                         .len = component_name_lengths[component_handle - 1] };
}

static void set_component_name(
    GglComponentHandle handle, GglBuffer component_name
) {
    assert(handle != 0);
    assert(handle <= GGL_MAX_GENERIC_COMPONENTS);
    assert(component_name.len < MAX_COMPONENT_NAME_LENGTH);

    memcpy(
        component_names[handle - 1], component_name.data, component_name.len
    );
    component_name_lengths[handle - 1] = (uint8_t) component_name.len;
}

GglError ggl_ipc_components_get_handle(
    GglBuffer svcuid, GglComponentHandle *component_handle
) {
    assert(component_handle != NULL);

    if (AUTH_ENABLED) {
        // Match decoded SVCUID and return match

        if (svcuid.len != GGL_IPC_SVCUID_LEN) {
            GGL_LOGE("ipc-server", "svcuid is invalid length.");
            return GGL_ERR_INVALID;
        }

        GglBuffer svcuid_bin = GGL_BUF((uint8_t[SVCUID_BIN_LEN]) { 0 });
        bool decoded = ggl_base64_decode(svcuid, &svcuid_bin);
        if (!decoded) {
            GGL_LOGE("ipc-server", "svcuid is invalid base64.");
            return GGL_ERR_INVALID;
        }

        for (GglComponentHandle i = 1; i <= registered_components; i++) {
            GglBuffer svcuid_bin_i = GGL_BUF(svcuids[i - 1]);
            if (ggl_buffer_eq(svcuid_bin, svcuid_bin_i)) {
                *component_handle = i;
                return GGL_ERR_OK;
            }
        }

        GGL_LOGE("ipc-server", "Requested svcuid not registered.");
    } else {
        // Match name, and return match. Insert if not found.
        // Assume SVCUID == component name

        if (svcuid.len > MAX_COMPONENT_NAME_LENGTH) {
            GGL_LOGE("ipc-server", "svcuid is invalid length.");
            return GGL_ERR_INVALID;
        }

        for (GglComponentHandle i = 1; i <= registered_components; i++) {
            if (ggl_buffer_eq(svcuid, ggl_ipc_components_get_name(i))) {
                *component_handle = i;
                return GGL_ERR_OK;
            }
        }

        if (registered_components < GGL_MAX_GENERIC_COMPONENTS) {
            registered_components += 1;
            *component_handle = registered_components;
            set_component_name(*component_handle, svcuid);
            return GGL_ERR_OK;
        }

        GGL_LOGE("ipc-server", "Insufficent generic component slots.");
    }

    return GGL_ERR_NOENTRY;
}

static void get_svcuid(GglComponentHandle component_handle, GglBuffer *svcuid) {
    assert(svcuid->len >= GGL_IPC_SVCUID_LEN);
    if (AUTH_ENABLED) {
        GglBumpAlloc balloc = ggl_bump_alloc_init(*svcuid);
        (void) ggl_base64_encode(
            GGL_BUF(svcuids[component_handle - 1]), &balloc.alloc, svcuid
        );
    } else {
        *svcuid = ggl_ipc_components_get_name(component_handle);
    }
}

GglError ggl_ipc_components_register(
    int client_fd, GglComponentHandle *component_handle, GglBuffer *svcuid
) {
    uint8_t component_name_buf[MAX_COMPONENT_NAME_LENGTH];
    GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(component_name_buf));
    GglBuffer component_name;
    GglError ret
        = ggl_ipc_auth_lookup_name(client_fd, &balloc.alloc, &component_name);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    for (GglComponentHandle i = 1; i <= registered_components; i++) {
        if (ggl_buffer_eq(component_name, ggl_ipc_components_get_name(i))) {
            *component_handle = i;
            get_svcuid(i, svcuid);
            GGL_LOGD(
                "ipc-server",
                "Found existing auth info for component %.*s.",
                (int) component_name.len,
                component_name.data
            );
            return GGL_ERR_OK;
        }
    }

    if (registered_components >= GGL_MAX_GENERIC_COMPONENTS) {
        GGL_LOGE("ipc-server", "Insufficent generic component slots.");
        return GGL_ERR_NOMEM;
    }

    GGL_LOGD(
        "ipc-server",
        "Registering new svcuid for component %.*s.",
        (int) component_name.len,
        component_name.data
    );

    registered_components += 1;
    *component_handle = registered_components;
    set_component_name(*component_handle, component_name);

    ret = ggl_read_exact(random_fd, GGL_BUF(svcuids[*component_handle - 1]));
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("ipc-server", "Failed to read from /dev/random.");
        return GGL_ERR_FATAL;
    }
    get_svcuid(*component_handle, svcuid);

    return GGL_ERR_OK;
}
