// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GG_FLEET_STATUSD_FLEET_STATUS_SERVICE_H
#define GG_FLEET_STATUSD_FLEET_STATUS_SERVICE_H

#include <gg/error.h>
#include <gg/types.h>

#define MAX_THING_NAME_LEN 128

/// Publishes a fleet status update to the cloud. @p removed_components lists
/// component names (as buffers) that have just been uninstalled by a
/// deployment, if any; each is reported with status=UNINSTALLED so the cloud
/// can prune them from its inventory even on PARTIAL updates. Pass an empty
/// list when there are no removals.
GgError publish_fleet_status_update(
    GgBuffer thing_name,
    GgBuffer trigger,
    GgMap deployment_info,
    GgList removed_components
);

#endif // GG_FLEET_STATUSD_FLEET_STATUS_SERVICE_H
