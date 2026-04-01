// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GG_LITE_HEALTH_DAEMON_H
#define GG_LITE_HEALTH_DAEMON_H

//! gg-lite-health-daemon entry point

#include <gg/error.h>

/// Run the health daemon main loop.
GgError run_gg_lite_health_daemon(void);

#endif
