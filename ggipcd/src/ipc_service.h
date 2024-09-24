// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_IPC_SERVICE_H
#define GGL_IPC_SERVICE_H

#include <ggl/alloc.h>
#include <ggl/error.h>
#include <ggl/object.h>
#include <stdint.h>

typedef struct {
    GglBuffer component;
    GglBuffer service;
    GglBuffer operation;
} GglIpcOperationInfo;

typedef GglError GglIpcOperationHandler(
    const GglIpcOperationInfo *info,
    GglMap args,
    uint32_t handle,
    int32_t stream_id,
    GglAlloc *alloc
);

typedef struct {
    GglBuffer name;
    GglIpcOperationHandler *handler;
} GglIpcOperation;

typedef struct {
    GglBuffer name;
    GglIpcOperation *operations;
    uint8_t operation_count;
} GglIpcService;

GglIpcService ggl_ipc_service_pubsub;
GglIpcService ggl_ipc_service_mqttproxy;
GglIpcService ggl_ipc_service_config;
GglIpcService ggl_ipc_service_cli;

#endif
