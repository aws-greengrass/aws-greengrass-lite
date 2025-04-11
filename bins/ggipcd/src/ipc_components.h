// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_IPC_COMPONENTS_H
#define GGL_IPC_COMPONENTS_H

#include <ggl/buffer.h>
#include <ggl/error.h>
#include <stdint.h>

/// Maximum number of generic components that can be tracked.
/// This is unique component names over all time, not just at a given moment.
/// Can be configured with `-DGGL_MAX_GENERIC_COMPONENTS=<N>`.
#ifndef GGL_MAX_GENERIC_COMPONENTS
#define GGL_MAX_GENERIC_COMPONENTS 50
#endif

#define GGL_IPC_SVCUID_LEN 16

#if GGL_MAX_GENERIC_COMPONENTS <= UINT8_MAX
typedef uint8_t GglComponentHandle;
#elif GGL_MAX_GENERIC_COMPONENTS <= UINT16_MAX
typedef uint16_t GglComponentHandle;
#else
#error "Maximum number of generic components is too large."
#endif

/// Start the IPC component server used to verify svcuid.
GglError ggl_ipc_start_component_server(void);

/// Get a non-zero authentication handle associated with an SVCUID.
GglError ggl_ipc_components_get_handle(
    GglBuffer svcuid, GglComponentHandle *component_handle
);

/// Get a component's name
GglBuffer ggl_ipc_components_get_name(GglComponentHandle component_handle);

/// Authenticate client and create component entry and SVCUID.
GglError ggl_ipc_components_register(
    GglBuffer component_name,
    GglComponentHandle *component_handle,
    GglBuffer *svcuid
);

#endif
