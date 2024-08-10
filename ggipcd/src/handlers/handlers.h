// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_IPC_HANDLERS_H
#define GGL_IPC_HANDLERS_H

#include <ggl/alloc.h>
#include <ggl/error.h>
#include <ggl/object.h>
#include <pthread.h>

typedef GglError GglIpcHandler(
    GglMap args, uint32_t handle, int32_t stream_id, GglAlloc *alloc
);

GglIpcHandler handle_publish_to_iot_core;
GglIpcHandler handle_subscribe_to_iot_core;
GglIpcHandler handle_update_configuration;
GglIpcHandler handle_publish_to_topic;
GglIpcHandler handle_subscribe_to_topic;

#endif
