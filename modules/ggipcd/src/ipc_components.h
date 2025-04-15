// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_IPC_COMPONENTS_H
#define GGL_IPC_COMPONENTS_H

#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/ipc/common.h>
#include <stdint.h>

/// Maximum number of generic components that can be tracked.
/// This is unique component names over all time, not just at a given moment.
/// Can be configured with `-DGGL_MAX_GENERIC_COMPONENTS=<N>`.
#ifndef GGL_MAX_GENERIC_COMPONENTS
#define GGL_MAX_GENERIC_COMPONENTS 50
#endif

#if GGL_MAX_GENERIC_COMPONENTS <= UINT8_MAX
typedef uint8_t GglComponentHandle;
#elif GGL_MAX_GENERIC_COMPONENTS <= UINT16_MAX
typedef uint16_t GglComponentHandle;
#else
#error "Maximum number of generic components is too large."
#endif

/// Start the IPC component server used to verify svcuid.
GglError ggl_ipc_start_component_server(void);

/// Convert an SVCUID from string to binary format
GglError ggl_ipc_svcuid_from_str(GglBuffer svcuid, GglSvcuid *out);

/// Get a non-zero authentication handle associated with an SVCUID.
GglError ggl_ipc_components_get_handle(
    GglSvcuid svcuid, GglComponentHandle *component_handle
);

/// Get a component's name
GglBuffer ggl_ipc_components_get_name(GglComponentHandle component_handle);

/// Register component and get component handle and SVCUID.
GglError ggl_ipc_components_register(
    GglBuffer component_name,
    GglComponentHandle *component_handle,
    GglSvcuid *svcuid
);

#endif
