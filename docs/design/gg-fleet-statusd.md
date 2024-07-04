# GG Lite - Fleet Status Service Daemon Design

## Overview

The fleet status service is a crucial feature in GG Java which enables the 
Nucleus to collect and report the health statuses of deployed components to the 
cloud. This allows customers to track their device status in the console. We 
will be replicating this behavior for GG Lite, ensuring that customers can still 
see component statuses in the console just as they did with GG Java. This task 
will be handled by `gg-fleet-statusd`, an individual process part of GG Lite.


## Requirements

1. The daemon must use `iotcored` to send MQTT messages containing the device 
status to the cloud.
2. The daemon must send a fleet status update on startup.

**Note:** These requirements only cover the current scope for the design and 
will be expanded upon as we continue to add new features to gg-fleet-statusd.


## Startup

When Greengrass, and subsequently gg-fleet-statusd is started, gg-fleet-statusd 
will need to send a Fleet Status Service update to the cloud. This is to ensure 
that the console displays accurate health statuses in the case that the device 
was not running for an extended period of time or that it is being started for 
the first time.

We will need the following methods to accomplish this task:

```
void send_fleet_status_update_for_all_components(Trigger& trigger);

void upload_fleet_status_service_data(Trigger& trigger, GravelList components);

void publish_message(FleetStatusDetails& fleetStatusDetails, GravelList
 componentDetails, Trigger& trigger);
```

#### send_fleet_status_update_for_all_components:

This method will be called from `main`. It will trigger FSS to send a status 
update to the cloud for all components on the device.

The `Trigger` class will be used to describe the event triggering a fleet
status update, so that it may be handled accordingly. Currently it will consist
of one trigger, `STARTUP`. Future triggers will include cloud deployments, MQTT
reconnections, and cadence updates.


#### upload_fleet_status_service_data:

This method will be called from the above method. It will be used to collect 
component information for any components that an update is being sent for, such
as the component name, version, and health status. It will then construct a 
`FleetStatusDetails` which will contain data about the fleet and the cause for 
the update.


#### publish_message

This method will be used to publish any fleet status updates collected. In the 
future it will handle different triggers according to their specific behaviors,
but as of now it will simply use `iotcored` to publish a message containing the
`FleetStatusDetails` collected in the above method.


## Offline Capabilities

In this current stage of the design, fleet status service will not support any
offline capabilities. It will simply send a fleet status update on startup if 
an MQTT connection is present. In the future, status updates will be sent on 
any instances of MQTT reconnections as well, ensuring that the device status is
visible in the cloud as soon as it has a network connection.
