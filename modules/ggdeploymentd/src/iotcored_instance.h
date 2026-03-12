// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_IOTCORED_INSTANCE_H
#define GGDEPLOYMENTD_IOTCORED_INSTANCE_H

#include <gg/error.h>
#include <gg/types.h>
#include <ggl/process.h>
#include <stdint.h>

typedef struct {
    GglProcessHandle handle;
} IotcoredInstance;

/// Spawn an iotcored process pointed at the given endpoint. The spawned
/// process reads cert/key/rootca from ggconfigd — only the endpoint is
/// overridden.
GgError iotcored_instance_start(
    IotcoredInstance *ctx, GgBuffer iotcored_path, GgBuffer endpoint
);

/// Block until the named iotcored instance reports connected=true or timeout
/// expires. Retries the subscribe call to handle instances that haven't
/// started their Core Bus server yet.
GgError iotcored_await_connection(GgBuffer socket_name, uint32_t timeout_s);

/// Kill the spawned iotcored process. Can be used directly with GG_CLEANUP.
void iotcored_instance_stop(IotcoredInstance *ctx);

#endif
