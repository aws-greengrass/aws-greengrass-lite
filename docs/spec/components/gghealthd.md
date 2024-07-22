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

### list_status

Retrieves the lifecycle states of all Greengrass components in the active
config. This API is intended to be used by processes which report deployment and
core device status updates.

#### Parameters

- [gghealthd-bus-list-status-1] All parameters shall be ignored.

#### Response

- [gghealthd-bus-list-status-resp-1] The response shall be a list where each
  element is a map with the response schema specified in the `get_status`
  response.

#### Errors

- [gghealthd-bus-list-status-error-1] `GGL_ERR_NOCONN` shall be returned if
  gghealthd is unable to establish a connection with the orchestrator.
- [gghealthd-bus-list-status-error-2] `GGL_ERR_FATAL` shall be returned for any
  `gghealthd` internal `gghealthd` error in communication with the orchestrator.
  A possible reason for this return is a mismatched orchestrator version for
  which `gghealthd` was not developed.
- [gghealthd-bus-list-status-error-3] `GGL_ERR_FATAL` shall be returned in the
  event that `gghealthd` is not permitted to view a component's status.
  - [gghealthd-bus-list-status-error-3.1] In the event of `GGL_ERR_FATAL`,
    `gghealthd` shall still provide the response to the best of its ability. All
    components which `gghealthd` cannot examine shall be marked `BROKEN` (i.e.
    this will flag the Greengrass Core Device as Unhealthy).

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
