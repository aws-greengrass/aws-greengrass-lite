# `gg-fleet-statusd` spec

`gg-fleet-statusd` sends status reports to the AWS IoT Greengrass Fleet Status
Service. This component will make the device show up in the Greengrass console
as a Greengrass core device.

See docs at
<https://docs.aws.amazon.com/greengrass/v2/developerguide/device-status.html>.

- [fss-1] The daemon uses `iotcored` to report device status.
- [fss-2] A devices using this daemon shows up in the Greengrass Console.
- [fss-3] Fleet status updates are sent at a configured cadence.
- [fss-4] Fleet status updates are sent on startup and at any instances of mqtt
  reconnection.

## CLI parameters

## Environment Variables

## Core Bus API

Each of the APIs below take a single map as the argument to the call, with the
key-value pairs described by the parameters listed in their respective sections.

### update_component_status

The update_component_status method allows a component to share its health status
with gg-fleet-statusd, which will then be shared with the cloud.

- [gg-fleet-statusd-update_component_status-1] `component_name` is a required
  parameter of type buffer
  - [gg-fleet-statusd-update_component_status-1.1] `component_name` holds the
    name of the component sending a status update.
- [gg-fleet-statusd-update_component_status-2] `new_status` is a required
  parameter of type buffer
  - [gg-fleet-statusd-update_component_status-2.1] `new_status` describes the
    status the component wants reported to the cloud
  - [gg-fleet-statusd-update_component_status-2.2] `new_status` can hold the
    following statuses:
    - `ERRORED`
    - `BROKEN`
    - `RUNNING`
    - `FINISHED`
