# GG Lite - Fleet Status Service Daemon Design

See [gg-fleet-statusd spec](../spec/components/gg-fleet-statusd.md) for the
public interface for gg-fleet-statusd.

## Overview

The fleet status service enables Greengrass to collect and report the health
status of deployed components to the cloud. This allows customers to track their
device status in the console. GG Lite will replicate GG Classic behavior,
ensuring that customers can still see component statuses in the console just as
they do with Classic. This task will be handled by `gg-fleet-statusd`, an
individual process part of GG Lite.

## Requirements

1. The daemon must use `iotcored` to send MQTT messages containing the device
   status to the cloud.
2. The daemon must receive component health statuses from `gghealthd`.
3. The daemon must send a fleet status update on startup.

**Note:** These requirements only cover the current scope for the design and
will be expanded upon as we continue to add new features to gg-fleet-statusd.

## Startup

1. On startup, `gg-fleet-statusd` will use `gghealthd` to retrieve the health
   status of all components on the device.
2. The daemon will use `iotcored` to publish a fleet status update to IoT Core,
   containing the health statuses collected in the previous step.

## Offline Capabilities

In this current stage of the design, fleet status service will not support any
offline capabilities. It will simply send a fleet status update on startup if an
MQTT connection is present. In the future, status updates will be sent on any
instances of MQTT reconnections as well, ensuring that the device status is
visible in the cloud as soon as it has a network connection.
