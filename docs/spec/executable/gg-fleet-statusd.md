# `gg-fleet-statusd` spec

`gg-fleet-statusd` sends status reports to the AWS IoT Greengrass Fleet Status
Service. This component will make the device show up in the Greengrass console
as a Greengrass core device.

See docs at
<https://docs.aws.amazon.com/greengrass/v2/developerguide/device-status.html>.

- [fss-1] The daemon uses `iotcored` to report device status.
- [fss-2] The daemon uses `gghealthd` to collect component health statuses.
- [fss-3] A devices using this daemon shows up in the Greengrass Console.
- [fss-4] Fleet status updates are sent at a configured cadence.
- [fss-5] Fleet status updates are sent on startup and at any instances of mqtt
  reconnection.

## CLI parameters

## Environment Variables

## Core Bus API

Each of the APIs below take a single map as the argument to the call, with the
key-value pairs described by the parameters listed in their respective sections.

### send_fleet_status_update

The send_fleet_status_update allows other components to trigger gg-fleet-statusd
to send a fleet status update to IoT Core.

- [gg-fleet-statusd-send_fleet_status_update-1] `trigger` is a required
  parameter of type buffer
  - [gg-fleet-statusd-send_fleet_status_update-1.1] `trigger` describes the
    event causing the update
  - [gg-fleet-statusd-send_fleet_status_update-1.2] `trigger` can hold the
    following values:
    - `LOCAL_DEPLOYMENT`
    - `THING_DEPLOYMENT`
    - `THING_GROUP_DEPLOYMENT`
    - `COMPONENT_STATUS_CHANGE`
    - `RECONNECT`
    - `LAUNCH`
    - `NETWORK_RECONFIGURE`
- [gg-fleet-statusd-send_fleet_status_update-2] `deployment_info` is a required
  parameter of type map
  - [gg-fleet-statusd-send_fleet_status_update-2.1] `deployment_info` includes a
    map of deployment information to send to cloud after a deployment
