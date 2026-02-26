// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGHEALTHD_HEALTH_H
#define GGHEALTHD_HEALTH_H

#include <gg/error.h>
#include <gg/types.h>

GgError gghealthd_init(void);

// get status from native orchestrator or local database
GgError gghealthd_get_status(GgBuffer component_name, GgBuffer *status);

// update status (with GG component lifecycle state) in
// native orchestrator or local database
GgError gghealthd_update_status(GgBuffer component_name, GgBuffer status);

GgError gghealthd_get_health(GgBuffer *status);

GgError gghealthd_restart_component(GgBuffer component_name);

#endif
