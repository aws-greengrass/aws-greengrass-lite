# `gghealthd` design

See [gghealthd spec](../spec/components/gghealthd.md) for the public interface
for gghealthd.

gghealthd is intended to forward and serve component lifecycle state updates
between gghealthd's clients and the Greengrass Lite core device's orchestrator.
Communication with the orchestrator requires its own binary ABI or network
protocol depending on the system. Therefore, gghealthd is an abstraction over a
subset orchestration's API. In order to support state updates via Greengrass
Classic IPC over ggipcd, gghealthd shall be implemented as a core component, a
service, with permissions to update a component's orchestration state on its
behalf.

# Responsibilities

![Data flow of gghealthd responsibilities. All communication between other processes is done over core bus, with translation to/from generic components via `ggipcd`](gghealthd.svg)

- On startup, `gghealthd` shall begin serving the `gg_health` Core Bus API
  interface.
- When recipe-runner executes, it may send state updates to `gghealthd` for
  `install` and `run` scripts.
- When a generic component executes its `start` script, it may send state
  updates over the IPC bridge.
- When a deployment starts, gghealthd must receive a subscription request from
  `ggdeploymentd`. In response, gghealthd shall connect to the core device
  orchestrator and await Greengrass component completions or failures,
  forwarding the overall result back to ggdeploymentd.
- On request by `gg-fleet-statusd`, `gghealthd` shall report individual
  component lifecycle states. This will be used to report the overall device
  health as well as what components are installed on the device.
- `gghealthd` retrieves requested component information from `ggconfigd` for
  input validation.
