// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGHEALTHD_SUBSCRIPTIONS_H
#define GGHEALTHD_SUBSCRIPTIONS_H

#include <ggl/buffer.h>
#include <ggl/error.h>
#include <stdint.h>

GglError gghealthd_register_lifecycle_subscription(
    GglBuffer component_name, uint32_t handle
);

void gghealthd_unregister_lifecycle_subscription(void *ctx, uint32_t handle);

void *health_event_loop_thread(void *ctx);

#endif
