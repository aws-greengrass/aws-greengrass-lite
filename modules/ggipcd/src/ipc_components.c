// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ipc_components.h"
#include <assert.h>
#include <gg/base64.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/flags.h>
#include <gg/ipc/limits.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/rand.h>
#include <ggl/core_bus/server.h>
#include <ggl/nucleus/constants.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>

/// Maximum length of generic component name.
#define MAX_COMPONENT_NAME_LENGTH (128)

static_assert(
    GG_IPC_SVCUID_STR_LEN % 4 == 0,
    "GG_IPC_SVCUID_STR_LEN must be a multiple of 4."
);

static_assert(
    sizeof((GglSvcuid) { 0 }.val) == ((size_t) GG_IPC_SVCUID_STR_LEN / 4 * 3),
    "GG_IPC_SVCUID_STR_LEN must match size of GglSvcuid val, base64 encoded."
);

static pthread_mutex_t ggl_ipc_component_registered_components_mtx
    = PTHREAD_MUTEX_INITIALIZER;
static GglSvcuid svcuids[GGL_MAX_GENERIC_COMPONENTS];
static uint8_t component_names[GGL_MAX_GENERIC_COMPONENTS]
                              [MAX_COMPONENT_NAME_LENGTH];
static uint8_t component_name_lengths[GGL_MAX_GENERIC_COMPONENTS];

static GglComponentHandle registered_components = 0;

GgError ggl_ipc_svcuid_from_str(GgBuffer svcuid, GglSvcuid *out) {
    if (svcuid.len != GG_IPC_SVCUID_STR_LEN) {
        return GG_ERR_INVALID;
    }
    GglSvcuid result = { 0 };
    bool decoded = gg_base64_decode(svcuid, &GG_BUF(result.val));
    if (!decoded) {
        GG_LOGE("svcuid is invalid base64.");
        return GG_ERR_INVALID;
    }
    *out = result;
    return GG_ERR_OK;
}

GgBuffer ggl_ipc_components_get_name(GglComponentHandle component_handle) {
    assert(component_handle != 0);
    // coverity[missing_lock]
    assert(component_handle <= registered_components);
    return (GgBuffer) { .data = component_names[component_handle - 1],
                        .len = component_name_lengths[component_handle - 1] };
}

static void set_component_name(
    GglComponentHandle handle, GgBuffer component_name
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

static GgError verify_svcuid(void *ctx, GgMap params, uint32_t handle) {
    (void) ctx;

    GgObject *svcuid_obj;

    GgError ret = gg_map_validate(
        params,
        GG_MAP_SCHEMA(
            { GG_STR("svcuid"), GG_REQUIRED, GG_TYPE_BUF, &svcuid_obj },
        )
    );

    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to validate verify_svcuid parameters.");
        return GG_ERR_INVALID;
    }

    GglSvcuid svcuid;
    ret = ggl_ipc_svcuid_from_str(gg_obj_into_buf(*svcuid_obj), &svcuid);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ggl_respond(
        handle,
        gg_obj_bool(ggl_ipc_components_get_handle(svcuid, NULL) == GG_ERR_OK)
    );

    return GG_ERR_OK;
}

static void *ggl_ipc_component_server(void *args) {
    (void) args;

    GglRpcMethodDesc handlers[] = {
        { GG_STR("verify_svcuid"), false, verify_svcuid, NULL },
    };
    size_t handlers_len = sizeof(handlers) / sizeof(handlers[0]);

    GgBuffer interface = GG_STR("ipc_component");

    GgError ret = ggl_listen(interface, handlers, handlers_len);

    GG_LOGE("Exiting with error %u.", (unsigned) ret);

    return NULL;
}

GgError ggl_ipc_start_component_server(void) {
    pthread_t ptid;
    int res = pthread_create(&ptid, NULL, &ggl_ipc_component_server, NULL);
    if (res != 0) {
        GG_LOGE(
            "Failed to create ggl_ipc_component_server with error %d.", res
        );
        return GG_ERR_FATAL;
    }

    res = pthread_detach(ptid);
    if (res != 0) {
        GG_LOGE(
            "Failed to detach the ggl_ipc_component_server thread with error %d.",
            res
        );
        return GG_ERR_FATAL;
    }

    return GG_ERR_OK;
}

GgError ggl_ipc_components_get_handle(
    GglSvcuid svcuid, GglComponentHandle *component_handle
) {
    GG_MTX_SCOPE_GUARD(&ggl_ipc_component_registered_components_mtx);

    // Match decoded SVCUID and return match

    for (GglComponentHandle i = 1; i <= registered_components; i++) {
        if (memcmp(svcuid.val, get_svcuid(i).val, sizeof(svcuid.val)) == 0) {
            if (component_handle != NULL) {
                *component_handle = i;
            }
            return GG_ERR_OK;
        }
    }

    GG_LOGE("Requested svcuid not registered.");

    return GG_ERR_NOENTRY;
}

GgError ggl_ipc_components_register(
    GgBuffer component_name,
    GglComponentHandle *component_handle,
    GglSvcuid *svcuid
) {
    GG_MTX_SCOPE_GUARD(&ggl_ipc_component_registered_components_mtx);

    for (GglComponentHandle i = 1; i <= registered_components; i++) {
        if (gg_buffer_eq(component_name, ggl_ipc_components_get_name(i))) {
            *component_handle = i;
            *svcuid = get_svcuid(i);
            GG_LOGD(
                "Found existing auth info for component %.*s.",
                (int) component_name.len,
                component_name.data
            );
            return GG_ERR_OK;
        }
    }

    if (registered_components >= GGL_MAX_GENERIC_COMPONENTS) {
        GG_LOGE("Insufficent generic component slots.");
        return GG_ERR_NOMEM;
    }

    GG_LOGD(
        "Registering new svcuid for component %.*s.",
        (int) component_name.len,
        component_name.data
    );

    registered_components += 1;
    *component_handle = registered_components;
    set_component_name(*component_handle, component_name);

    gg_rand_fill(GG_BUF(svcuids[*component_handle - 1].val));
    *svcuid = get_svcuid(*component_handle);

    return GG_ERR_OK;
}
