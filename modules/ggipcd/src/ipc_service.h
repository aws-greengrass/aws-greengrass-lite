// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_IPC_SERVICE_H
#define GGL_IPC_SERVICE_H

#include "ipc_error.h"
#include <gg/arena.h>
#include <gg/error.h>
#include <gg/types.h>
#include <stdint.h>

typedef struct {
    GgBuffer component;
    GgBuffer service;
    GgBuffer operation;
} GglIpcOperationInfo;

typedef GgError GglIpcOperationHandler(
    const GglIpcOperationInfo *info,
    GgMap args,
    uint32_t handle,
    int32_t stream_id,
    GglIpcError *ipc_error,
    GgArena *alloc
);

typedef struct {
    GgBuffer name;
    GglIpcOperationHandler *handler;
} GglIpcOperation;

typedef struct {
    GgBuffer name;
    GglIpcOperation *operations;
    uint8_t operation_count;
} GglIpcService;

extern GglIpcService ggl_ipc_service_pubsub;
extern GglIpcService ggl_ipc_service_mqttproxy;
extern GglIpcService ggl_ipc_service_config;
extern GglIpcService ggl_ipc_service_cli;
extern GglIpcService ggl_ipc_service_private;
extern GglIpcService ggl_ipc_service_lifecycle;
extern GglIpcService ggl_ipc_service_token_validation;

#endif
