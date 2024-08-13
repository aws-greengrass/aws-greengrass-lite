# `gghealthd` spec

`gghealthd` allows platform-agnostic processes to connect and use a subset of
orchestrator functionality. `gghealthd` will allow services like
`gg-fleet-statusd` or `ggdeploymentd` to measure and report core device status
updates.

- [gghealthd-1] `gghealthd` shall be deployed with sufficient permissions in
  order to forward RPC requests to the orchestrator. In particular, the Core Bus
  API function `get_status` may require additional permissions to update a
  component's state inside an orchestrator on its behalf.
- [gghealthd-2] `gghealthd` shall attempt exponential retry if it is unable to
  establish a connection with the orchestrator.
  - [gghealthd-2.1] `gghealthd` shall report itself as `ERRORED` if it is unable
    to connect to the orchestrator within 30 seconds of attempts.
  - [gghealthd-2.2] The connection between `gghealthd` and the orchestrator may
    be made as-needed (i.e. only connected when serving an RPC).

## Core Bus API

### Common Error Codes

| Error Code         | Reason                                          |
| ------------------ | ----------------------------------------------- |
| GGL_ERR_INVALID    | Parameter validation failed                     |
| GGL_ERR_NOMEM      | Allocation failed                               |
| GGL_ERR_NOENTRY(1) | Component not in config                         |
| GGL_ERR_NOENTRY(2) | No such method                                  |
| GGL_ERR_NOCONN     | Unable to contact Orchestrator                  |
| GGL_ERR_FATAL      | Protocol or permissions error with Orchestrator |

### get_status

Retrieves the lifecycle state of the requested component.

#### Parameters

- [gghealthd-bus-get-status-1] `component_name` is a required parameter of type
  buffer.
  - [gghealthd-bus-get-status-1.1] `component_name` shall contain the
    fully-qualified name of a component (e.g. `com.greengrass.sample`).
  - [gghealthd-bus-get-status-1.2] `component_name` shall be present in the
    active services config.

#### Response

- [gghealthd-bus-get-status-resp-1] `component_name` is the name of the
  component requested of type buffer.
  - [gghealthd-bus-get-status-resp-1.1] `component_name` shall be identical in
    size and contents to the requested `component_name`
- [gghealthd-bus-get-status-resp-2] `lifecycle_state` is the name of the
  lifecycle state the component is in, of type buffer.
  - [gghealthd-bus-get-status-resp-2.1] `lifecycle_state` shall be one of the
    following:
    - `NEW`
    - `INSTALLED`
    - `STARTING`
    - `RUNNING`
    - `STOPPING`
    - `FINISHED`
    - `ERRORED`
    - `BROKEN`
  - [gghealthd-bus-get-status-resp-2.2] `lifecycle_state` shall be `NEW` if the
    component cannot be retrieved from the orchestrator.
  - [gghealthd-bus-get-status-resp-2.3] if `component_name` requested is
    `gghealthd`, then `lifecycle_state` shall be reported as one of the
    following:
    - `RUNNING`
    - `ERRORED`
    - `BROKEN`

#### Errors

- [gghealthd-bus-get-status-error-1] `GGL_ERR_INVALID` shall be returned if the
  parameters received are not valid.
- [gghealthd-bus-get-status-error-2] `GGL_ERR_NOENTRY` shall be returned if the
  component does not exist in the active config.
- [gghealthd-bus-get-status-error-3] `GGL_ERR_NOCONN` shall be returned if
  gghealthd is unable to establish a connection with the orchestrator.
- [gghealthd-bus-get-status-error-4] `GGL_ERR_FATAL` shall be returned for any
  `gghealthd` internal `gghealthd` error in communication with the orchestrator.
  A possible reason for this return is a mismatched orchestrator version for
  which `gghealthd` was not developed.
- [gghealthd-bus-get-status-error-5] `GGL_ERR_FATAL` shall be returned in the
  event that `gghealthd` is not permitted to view a component's status.

### get_health

Measures the Core Device health status. This is taken by retrieving component
statuses for all root components in the Greengrass config. If any are broken,
then the device is unhealthy, else healthy.

#### Parameters

- [gghealthd-bus-get-health-1] All parameters shall be ignored.

#### Response

- [gghealthd-bus-get-health-resp-1] The response shall be a map with the
  following schema:
  - [gghealthd-bus-get-health-resp-1.1] Required key "health" shall be in the
    map with the value type Buffer
  - [gghealthd-bus-get-health-resp-1.2] The allowed values for key "health"
    shall be "UNHEALTHY" and "HEALTHY"

#### Errors

- [gghealthd-bus-get-health-error-1] `GGL_ERR_NOCONN` shall be returned if
  gghealthd is unable to establish a connection with the orchestrator.
- [gghealthd-bus-get-health-error-2] `GGL_ERR_FATAL` shall be returned for any
  `gghealthd` internal `gghealthd` error in communication with the orchestrator.
  A possible reason for this return is a mismatched orchestrator version for
  which `gghealthd` was not developed.
- [gghealthd-bus-get-health-error-3] `GGL_ERR_FATAL` shall be returned in the
  event that `gghealthd` is not permitted to view a root component's status.

### update_status

Updates a component to a new lifecycle state, replicating this state update to
the orchestrator. This API is intended to bridge the Greengrass Classic
`UpdateState` API which is used for `start` generic component lifecycle steps
which allow component-managed state transitions over IPC.

> Note: `UpdateState` only allows the new states "RUNNING" and "ERRORED", and
> only during a component's `start` step. Whichever IPC bridge is forwarding
> `UpdateState` shall do their own validation in order to report API errors
> correctly.

#### Parameters

- [gghealthd-bus-update-status-1] `component_name` is a required parameter of
  type buffer
  - [gghealthd-bus-update-status-1.1] `component_name` shall contain the
    fully-qualified name of a component (e.g. `com.greengrass.sample`).
- [gghealthd-bus-update-status-2] `lifecycle_state` is the name of the lifecycle
  state the component transitions to, of type buffer.
  - [gghealthd-bus-update-status-2.1] `lifecycle_state` shall be one of the
    following:
    - `RUNNING`
    - `STOPPING`
    - `ERRORED`
    - `BROKEN`
  - [gghealthd-bus-update-status-2.2] Any allowed lifecycle states not
    understood by the underlying orchestrator may be acknowledged with no action
    taken.

#### Response

- [gghealthd-bus-update-status-resp-1] The response shall be Null. That is, the
  response is simply an acknowledgement.

#### Errors

- [gghealthd-bus-update-status-error-1] `GGL_ERR_INVALID` shall be returned if
  the parameters received are not valid.
- [gghealthd-bus-update-status-error-2] `GGL_ERR_NOENTRY` shall be returned if
  the component does not exist in the active config.
- [gghealthd-bus-update-status-error-3] `GGL_ERR_NOCONN` shall be returned if
  gghealthd is unable to establish a connection with the orchestrator.
- [gghealthd-bus-update-status-error-4] `GGL_ERR_FATAL` shall be returned for
  any `gghealthd` internal `gghealthd` error in communication with the
  orchestrator. A possible reason for this return is a mismatched orchestrator
  version for which `gghealthd` was not developed.
- [gghealthd-bus-update-status-error-5] `GGL_ERR_FATAL` shall be returned in the
  event that `gghealthd` is not permitted to update another component's status.

### subscribe_to_deployment_updates

This subscription is intended to be used by `ggdeploymentd` to signal an ongoing
deployment for which it awaits the result. This API sends a response when all
components succeed or any fail.

[gghealthd-bus-deployment-updates-1] `gghealthd` shall respond with the result
of a deployment within 10 seconds of the final root component status update
becoming available.

#### Response

- [gghealthd-bus-deployment_updates-resp-1] The response shall be a map with the
  following schema:
  - [gghealthd-bus-deployment_updates-resp-1.1] Required key "result" shall be
    in the map with the value type Buffer
  - [gghealthd-bus-deployment_updates-resp-1.2] The allowed values for key
    "result" shall be "SUCCESS" and "FAILURE"

#### Errors

- [gghealthd-bus-deployment-updates-error-1] `GGL_ERR_NOCONN` shall be returned
  if gghealthd is unable to establish a connection with the orchestrator.
- [gghealthd-bus-deployment-updates-error-2] `GGL_ERR_FATAL` shall be returned
  for any `gghealthd` internal `gghealthd` error in communication with the
  orchestrator. A possible reason for this return is a mismatched orchestrator
  version for which `gghealthd` was not developed.
- [gghealthd-bus-deployment-updates-error-3] `GGL_ERR_FATAL` shall be returned
  in the event that `gghealthd` is not permitted to view a root component's
  status.
