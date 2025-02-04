// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ipc_components.h"
#include <sys/types.h>
#include <assert.h>
#include <ggipc/auth.h>
#include <ggl/base64.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/cleanup.h>
#include <ggl/core_bus/server.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/rand.h>
#include <pthread.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef GGL_IPC_AUTH_DISABLE
#warning "INSECURE!!! IPC authentication disabled!"

__attribute__((constructor)) static void warn_auth_disabled(void) {
    GGL_LOGE("INSECURE!!! IPC authentication disabled!");
    GGL_LOGE("SVCUID handling is in debug mode.");
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

static pthread_mutex_t ggl_ipc_component_registered_components_mtx
    = PTHREAD_MUTEX_INITIALIZER;
static uint8_t svcuids[GGL_MAX_GENERIC_COMPONENTS][SVCUID_BIN_LEN];
static uint8_t component_names[GGL_MAX_GENERIC_COMPONENTS]
                              [MAX_COMPONENT_NAME_LENGTH];
static uint8_t component_name_lengths[GGL_MAX_GENERIC_COMPONENTS];

static GglComponentHandle registered_components = 0;

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

GglError ipc_svcuid_exists(GglBuffer svcuid_buf) {
    GglBuffer svcuid_bin = GGL_BUF((uint8_t[SVCUID_BIN_LEN]) { 0 });
    bool decoded = ggl_base64_decode(svcuid_buf, &svcuid_bin);
    if (!decoded) {
        GGL_LOGE("svcuid is invalid base64.");
        return GGL_ERR_INVALID;
    }

    GGL_MTX_SCOPE_GUARD(&ggl_ipc_component_registered_components_mtx);

    for (GglComponentHandle i = 1; i <= registered_components; i++) {
        GglBuffer svcuid_bin_i = GGL_BUF(svcuids[i - 1]);
        if (ggl_buffer_eq(svcuid_bin, svcuid_bin_i)) {
            GGL_LOGD("Found the requested svcuid.");
            return GGL_ERR_OK;
        }
    }
    return GGL_ERR_FAILURE;
}

static GglError verify_svcuid(void *ctx, GglMap params, uint32_t handle) {
    (void) ctx;

    GglObject *svcuid_obj;

    GglError ret = ggl_map_validate(
        params,
        GGL_MAP_SCHEMA({ GGL_STR("svcuid"), true, GGL_TYPE_BUF, &svcuid_obj }, )
    );

    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to validate the map provided to 'verify_svcuid'.");
        return GGL_ERR_INVALID;
    }

    if (svcuid_obj->type != GGL_TYPE_BUF) {
        GGL_LOGE("svcuid must be of type buffer.");
        return GGL_ERR_INVALID;
    }

    if (ipc_svcuid_exists(svcuid_obj->buf) == GGL_ERR_OK) {
        ggl_respond(handle, GGL_OBJ_BOOL(true));
        return GGL_ERR_OK;
    }

    GGL_LOGE("Requested svcuid not found.");

    ggl_respond(handle, GGL_OBJ_BOOL(false));
    return GGL_ERR_OK;
}

static void *ggl_ipc_component_server(void *args) {
    (void) args;

    GglRpcMethodDesc handlers[] = {
        { GGL_STR("verify_svcuid"), false, verify_svcuid, NULL },
    };
    size_t handlers_len = sizeof(handlers) / sizeof(handlers[0]);

    GglBuffer interface = GGL_STR("ipc_component");

    GglError ret = ggl_listen(interface, handlers, handlers_len);

    GGL_LOGE("Exiting with error %u.", (unsigned) ret);

    return NULL;
}

GglError ggl_ipc_start_component_server(void) {
    pthread_t ptid;
    int res = pthread_create(&ptid, NULL, &ggl_ipc_component_server, NULL);
    if (res != 0) {
        GGL_LOGE(
            "Failed to create ggl_ipc_component_server with error %d.", res
        );
        return GGL_ERR_FATAL;
    }

    res = pthread_detach(ptid);
    if (res != 0) {
        GGL_LOGE(
            "Failed to detach the ggl_ipc_component_server thread with error "
            "%d.",
            res
        );
        return GGL_ERR_FATAL;
    }

    return GGL_ERR_OK;
}

GglError ggl_ipc_components_get_handle(
    GglBuffer svcuid, GglComponentHandle *component_handle
) {
    assert(component_handle != NULL);

    GGL_MTX_SCOPE_GUARD(&ggl_ipc_component_registered_components_mtx);

    if (AUTH_ENABLED) {
        // Match decoded SVCUID and return match

        if (svcuid.len != GGL_IPC_SVCUID_LEN) {
            GGL_LOGE("svcuid is invalid length.");
            return GGL_ERR_INVALID;
        }

        GglBuffer svcuid_bin = GGL_BUF((uint8_t[SVCUID_BIN_LEN]) { 0 });
        bool decoded = ggl_base64_decode(svcuid, &svcuid_bin);
        if (!decoded) {
            GGL_LOGE("svcuid is invalid base64.");
            return GGL_ERR_INVALID;
        }

        for (GglComponentHandle i = 1; i <= registered_components; i++) {
            GglBuffer svcuid_bin_i = GGL_BUF(svcuids[i - 1]);
            if (ggl_buffer_eq(svcuid_bin, svcuid_bin_i)) {
                *component_handle = i;
                return GGL_ERR_OK;
            }
        }

        GGL_LOGE("Requested svcuid not registered.");
    } else {
        // Match name, and return match. Insert if not found.
        // Assume SVCUID == component name

        if (svcuid.len > MAX_COMPONENT_NAME_LENGTH) {
            GGL_LOGE("svcuid is invalid length.");
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

        GGL_LOGE("Insufficent generic component slots.");
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
                "Found existing auth info for component %.*s.",
                (int) component_name.len,
                component_name.data
            );
            return GGL_ERR_OK;
        }
    }

    if (registered_components >= GGL_MAX_GENERIC_COMPONENTS) {
        GGL_LOGE("Insufficent generic component slots.");
        return GGL_ERR_NOMEM;
    }

    GGL_LOGD(
        "Registering new svcuid for component %.*s.",
        (int) component_name.len,
        component_name.data
    );

    GGL_MTX_SCOPE_GUARD(&ggl_ipc_component_registered_components_mtx);
    registered_components += 1;
    *component_handle = registered_components;
    set_component_name(*component_handle, component_name);

    ret = ggl_rand_fill(GGL_BUF(svcuids[*component_handle - 1]));
    if (ret != GGL_ERR_OK) {
        return GGL_ERR_FATAL;
    }
    get_svcuid(*component_handle, svcuid);

    return GGL_ERR_OK;
}
