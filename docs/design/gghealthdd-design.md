# `gghealthd` design

See [gghealthd spec](../spec/components/gghealthd.md) for the public interface
for gghealthd.

gghealthd is intended to forward and serve component lifecycle state updates
between gghealthd's clients and the Greengrass Lite core device's orchestrator.
Communication with the orchestrator requires its own binary ABI or network
protocol depending on the system. Therefore, gghealthd is an abstraction over a
subset orchestration's API. In order to support state updates via Greengrass
Classic IPC over ipcbridged, gghealthd shall be implemented as a core component,
a service, with permissions to update a component's orchestration state on its
behalf.

# Responsibilities

![Data flow of gghealthd responsibilities. All communication between other components is done over the Core Bus, with translation to/from generic components via `ipcbridged`](gghealthd.svg)

- On startup, `gghealthd` shall begin serving the /aws/ggl/gghealthd Core Bus
  API interface.
- When recipe-runner executes, it may send state updates to `gghealthd` for
  `install` and `run` scripts.
- When a generic component executes its `start` script, it can send state
  updates over the IPC bridge.
- When a deployment starts, gghealthd must receive a subscription request from
  `ggdeploymentd`. In response, gghealthd shall connect to the core device
  orchestrator and await Greengrass component completions or failures,
  forwarding the overall result back to ggdeploymentd.
- On request by `gg-fleet-statusd`, `gghealthd` shall report core device health
  based on component status information maintained by the core device
  orchestrator.
- For all such requests, `gghealthd` shall request `ggconfig` for component
  configuration data (including name and version), and such requests shall be
  translated into appropriate core device orchestrator API calls.
