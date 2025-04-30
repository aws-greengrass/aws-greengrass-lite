// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ipc_components.h"
#include <assert.h>
#include <ggl/base64.h>
#include <ggl/buffer.h>
#include <ggl/cleanup.h>
#include <ggl/core_bus/server.h>
#include <ggl/error.h>
#include <ggl/flags.h>
#include <ggl/ipc/limits.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/rand.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>

/// Maximum length of generic component name.
#define MAX_COMPONENT_NAME_LENGTH (128)

static_assert(
    GGL_IPC_SVCUID_STR_LEN % 4 == 0,
    "GGL_IPC_SVCUID_STR_LEN must be a multiple of 4."
);

static_assert(
    sizeof((GglSvcuid) { 0 }.val) == ((size_t) GGL_IPC_SVCUID_STR_LEN / 4 * 3),
    "GGL_IPC_SVCUID_STR_LEN must match size of GglSvcuid val, base64 encoded."
);

static pthread_mutex_t ggl_ipc_component_registered_components_mtx
    = PTHREAD_MUTEX_INITIALIZER;
static GglSvcuid svcuids[GGL_MAX_GENERIC_COMPONENTS];
static uint8_t component_names[GGL_MAX_GENERIC_COMPONENTS]
                              [MAX_COMPONENT_NAME_LENGTH];
static uint8_t component_name_lengths[GGL_MAX_GENERIC_COMPONENTS];

static GglComponentHandle registered_components = 0;

GglError ggl_ipc_svcuid_from_str(GglBuffer svcuid, GglSvcuid *out) {
    if (svcuid.len != GGL_IPC_SVCUID_STR_LEN) {
        return GGL_ERR_INVALID;
    }
    GglSvcuid result;
    bool decoded = ggl_base64_decode(svcuid, &GGL_BUF(result.val));
    if (!decoded) {
        GGL_LOGE("svcuid is invalid base64.");
        return GGL_ERR_INVALID;
    }
    *out = result;
    return GGL_ERR_OK;
}

GglBuffer ggl_ipc_components_get_name(GglComponentHandle component_handle) {
    assert(component_handle != 0);
    // coverity[missing_lock]
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

static GglSvcuid get_svcuid(GglComponentHandle component_handle) {
    assert(component_handle != 0);
    assert(component_handle <= registered_components);
    return svcuids[component_handle - 1];
}

static GglError verify_svcuid(void *ctx, GglMap params, uint32_t handle) {
    (void) ctx;

    GglObject *svcuid_obj;

    GglError ret = ggl_map_validate(
        params,
        GGL_MAP_SCHEMA(
            { GGL_STR("svcuid"), GGL_REQUIRED, GGL_TYPE_BUF, &svcuid_obj },
        )
    );

    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to validate verify_svcuid parameters.");
        return GGL_ERR_INVALID;
    }

    GglSvcuid svcuid;
    ret = ggl_ipc_svcuid_from_str(ggl_obj_into_buf(*svcuid_obj), &svcuid);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ggl_respond(
        handle,
        ggl_obj_bool(ggl_ipc_components_get_handle(svcuid, NULL) == GGL_ERR_OK)
    );
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
    GglSvcuid svcuid, GglComponentHandle *component_handle
) {
    assert(component_handle != NULL);

    GGL_MTX_SCOPE_GUARD(&ggl_ipc_component_registered_components_mtx);

    // Match decoded SVCUID and return match

    for (GglComponentHandle i = 1; i <= registered_components; i++) {
        if (memcmp(svcuid.val, get_svcuid(i).val, sizeof(svcuid.val)) == 0) {
            if (component_handle != NULL) {
                *component_handle = i;
            }
            return GGL_ERR_OK;
        }
    }

    GGL_LOGE("Requested svcuid not registered.");

    return GGL_ERR_NOENTRY;
}

GglError ggl_ipc_components_register(
    GglBuffer component_name,
    GglComponentHandle *component_handle,
    GglSvcuid *svcuid
) {
    GGL_MTX_SCOPE_GUARD(&ggl_ipc_component_registered_components_mtx);

    for (GglComponentHandle i = 1; i <= registered_components; i++) {
        if (ggl_buffer_eq(component_name, ggl_ipc_components_get_name(i))) {
            *component_handle = i;
            *svcuid = get_svcuid(i);
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

    registered_components += 1;
    *component_handle = registered_components;
    set_component_name(*component_handle, component_name);

    GglError ret = ggl_rand_fill(GGL_BUF(svcuids[*component_handle - 1].val));
    if (ret != GGL_ERR_OK) {
        return GGL_ERR_FATAL;
    }
    *svcuid = get_svcuid(*component_handle);

    return GGL_ERR_OK;
}
