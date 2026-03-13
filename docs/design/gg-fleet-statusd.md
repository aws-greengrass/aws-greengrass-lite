# GG Lite - Fleet Status Service Daemon Design

See [gg-fleet-statusd spec](../spec/executable/gg-fleet-statusd.md) for the
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

## Configuration

The fleet status service supports the following configuration options:

### NucleusLite Configuration

- `services.aws.greengrass.NucleusLite.configuration.fleetStatus.periodicStatusPublishIntervalSeconds`:
  Configures the interval (in seconds) between periodic fleet status updates.
  Defaults to 86400 seconds (24 hours) if not specified.

### FleetStatusService Configuration

The service automatically initializes the following configuration at startup:

- `services.FleetStatusService.version`: Service version
- `services.FleetStatusService.sequenceNumber`: Sequence number for status
  updates (auto-incremented)

## Offline Capabilities

The fleet status service subscribes to MQTT connection status from `iotcored`.
On initial connection, a fleet status update is sent with a `NUCLEUS_LAUNCH`
trigger. On subsequent reconnections (e.g., after a network interruption), an
update is sent with a `RECONNECT` trigger. This satisfies spec requirement
`[fss-5]`.

A periodic update with a `CADENCE` trigger is also sent at a configurable
interval (default 24 hours).

**Note:** If `iotcored` itself restarts, the coreBus subscription is lost and
reconnection status updates will not be received until `gg-fleet-statusd` is
also restarted. See `TODO` in `connection_status_close_callback` in `entry.c`.
