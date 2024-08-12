// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggipc/auth.h"
#include <assert.h>
#include <ggl/base64.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef GGL_IPC_AUTH_DISABLE
#warning "INSECURE!!! IPC authentication disabled!"

__attribute__((constructor)) static void warn_auth_disabled(void) {
    GGL_LOGE("ggipcd", "INSECURE!!! IPC authentication disabled!");
    GGL_LOGE("ggipcd", "SVCUID handling is in debug mode.");
}
#endif

/// Maximum number of generic components that can be authenticated.
/// Can be configured with `-DGGL_MAX_GENERIC_COMPONENTS=<N>`.
#ifndef GGL_MAX_GENERIC_COMPONENTS
#define GGL_MAX_GENERIC_COMPONENTS 50
#endif

/// Maximum length of generic component name.
#define GGL_MAX_COMPONENT_NAME_LENGTH 128

#define SVCUID_BYTES 12

static uint8_t svcuids[GGL_MAX_GENERIC_COMPONENTS][SVCUID_BYTES];
static uint8_t component_names[GGL_MAX_GENERIC_COMPONENTS]
                              [GGL_MAX_COMPONENT_NAME_LENGTH];
static size_t component_name_lengths[GGL_MAX_GENERIC_COMPONENTS];

static size_t registered_components = 0;

GglError ggl_ipc_auth_get_component_name(
    GglBuffer svcuid, GglBuffer *component_name
) {
    assert(component_name != NULL);

#ifndef GGL_IPC_AUTH_DISABLE
    // Match decoded SVCUID and return match's name

    if (svcuid.len != (((size_t) SVCUID_BYTES / 4) * 3)) {
        GGL_LOGE("ipc-auth", "svcuid is invalid length.");
        return GGL_ERR_INVALID;
    }

    GglBuffer svcuid_b = GGL_BUF((uint8_t[SVCUID_BYTES]) { 0 });
    bool decoded = ggl_base64_decode(svcuid, &svcuid_b);
    if (!decoded) {
        GGL_LOGE("ipc-auth", "svcuid is invalid base64.");
        return GGL_ERR_INVALID;
    }

    for (size_t i = 0; i < registered_components; i++) {
        GglBuffer svcuid_i = { .data = svcuids[i], .len = SVCUID_BYTES };
        if (ggl_buffer_eq(svcuid_b, svcuid_i)) {
            *component_name = (GglBuffer) { .data = component_names[i],
                                            .len = component_name_lengths[i] };
            return GGL_ERR_OK;
        }
    }

    GGL_LOGE("ipc-auth", "Requested svcuid not registered.");
    return GGL_ERR_NOENTRY;
#else
    // Match name, and return stored name. Insert if new.
    // We need to return stored copy, as caller may assume output has static
    // lifetime.

    if (svcuid.len > GGL_MAX_COMPONENT_NAME_LENGTH) {
        GGL_LOGE("ipc-auth", "svcuid is invalid length.");
        return GGL_ERR_INVALID;
    }

    for (size_t i = 0; i < registered_components; i++) {
        GglBuffer component_name_i
            = { .data = component_names[i], .len = component_name_lengths[i] };
        if (ggl_buffer_eq(svcuid, component_name_i)) {
            *component_name = component_name_i;
            return GGL_ERR_OK;
        }
    }

    if (registered_components < GGL_MAX_GENERIC_COMPONENTS) {
        memcpy(component_names[registered_components], svcuid.data, svcuid.len);
        component_name_lengths[registered_components] = svcuid.len;
        *component_name = (GglBuffer
        ) { .data = component_names[registered_components], .len = svcuid.len };
        registered_components += 1;
        return GGL_ERR_OK;
    }

    GGL_LOGE("ipc-auth", "Insufficent generic component slots.");
    return GGL_ERR_NOENTRY;
#endif
}
