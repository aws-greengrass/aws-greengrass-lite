// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGHEALTHD_SUBSCRIPTIONS_H
#define GGHEALTHD_SUBSCRIPTIONS_H

#include <gg/error.h>
#include <gg/types.h>
#include <stdint.h>

GgError gghealthd_register_lifecycle_subscription(
    GgBuffer component_name, uint32_t handle
);

void gghealthd_unregister_lifecycle_subscription(void *ctx, uint32_t handle);

void init_health_events(void);

#endif
