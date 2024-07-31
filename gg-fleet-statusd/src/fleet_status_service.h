// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GG_FLEET_STATUSD_FLEET_STATUS_SERVICE_H
#define GG_FLEET_STATUSD_FLEET_STATUS_SERVICE_H

#include <ggl/error.h>
#include <ggl/object.h>

typedef enum Trigger {
    STARTUP
} Trigger;

typedef enum MessageType {
    PARTIAL,
    COMPLETE
} MessageType;

typedef struct {
    GglBuffer ggc_version;
    GglBuffer platform;
    GglBuffer architecture;
    GglBuffer thing;
    long sequence_number;
    long timestamp;
    Trigger trigger;
    MessageType message_type;

} FleetStatusDetails;

GglError publish_message(const char *thing_name);

// TODO: uncomment below and implement for full fss functionality

// void startup(const FssdArgs *arguments);
// void send_fleet_status_update_for_all_components(Trigger *trigger);
// void upload_fleet_status_service_data(Trigger *trigger, GglList components);
// void publish_message(FleetStatusDetails *fleetStatusDetails,
//                      GglList *componentDetails, Trigger *trigger);
// TODO: componentDetails needs to be its own object too, so this needs to be a
// ggl list of ggl objects?

#endif // GG_FLEET_STATUSD_FLEET_STATUS_SERVICE_H
