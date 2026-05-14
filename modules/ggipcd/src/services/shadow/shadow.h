// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_IPC_SERVICE_SHADOW_H
#define GGL_IPC_SERVICE_SHADOW_H

#include "../../ipc_service.h"

GglIpcOperationHandler ggl_handle_get_thing_shadow;
GglIpcOperationHandler ggl_handle_update_thing_shadow;
GglIpcOperationHandler ggl_handle_delete_thing_shadow;
GglIpcOperationHandler ggl_handle_list_named_shadows_for_thing;

#endif
