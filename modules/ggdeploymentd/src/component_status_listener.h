// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_COMPONENT_STATUS_LISTENER_H
#define GGDEPLOYMENTD_COMPONENT_STATUS_LISTENER_H

/// Starts a detached supervisor thread that subscribes to gghealthd component
/// lifecycle state changes and forwards them to the fleet status service (as a
/// COMPONENT_STATUS_CHANGE trigger) whenever no deployment is in progress. The
/// supervisor re-subscribes if the subscription drops (e.g. gghealthd restart).
void ggl_start_component_status_listener(void);

#endif
