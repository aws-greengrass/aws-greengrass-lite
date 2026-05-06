# Greengrass Nucleus Lite Design Overview

Greengrass nucleus lite is a collection of small systemd-managed daemons that
implement the AWS IoT Greengrass v2 runtime for resource-constrained Linux
devices. Daemons are intended to be small single-purpose executables that enable
a system to be composable by daemon selection.

## Daemon List

Each daemon is a standalone binary under `modules/<name>/bin/`. Daemons that
serve a core-bus interface listen on a Unix socket at
`/run/greengrass/<interface>`.

| Daemon               | Core-bus interface | Purpose                                                                                                     |
| -------------------- | ------------------ | ----------------------------------------------------------------------------------------------------------- |
| `ggconfigd`          | `gg_config`        | SQLite-backed hierarchical configuration store                                                              |
| `iotcored`           | `aws_iot_mqtt`     | MQTT connection to AWS IoT Core (coreMQTT-based)                                                            |
| `tesd`               | `aws_iot_tes`      | Token Exchange Service — fetches temporary AWS credentials via IoT credential provider                      |
| `tes-serverd`        | N/A                | Container-credentials HTTP endpoint for AWS SDKs inside components                                          |
| `ggdeploymentd`      | `gg_deployment`    | Deployment engine — processes cloud and local deployments                                                   |
| `gghealthd`          | `gg_health`        | Component lifecycle and health status via systemd sd-bus                                                    |
| `ggpubsubd`          | `gg_pubsub`        | Local publish/subscribe message bus between components                                                      |
| `gg-fleet-statusd`   | `gg_fleet_status`  | Publishes fleet status reports to IoT Core                                                                  |
| `ggipcd`             | N/A                | Translates GGv2 IPC protocol from components into core-bus calls                                            |
| `fleet-provisioning` | N/A                | Runs AWS Fleet Provisioning flow to register a device, then exits                                           |
| `recipe-runner`      | N/A                | Per-component helper invoked by systemd; expands recipe-script placeholders and execs the component process |
| `ggl-cli`            | N/A                | User-facing CLI for local deployments and management                                                        |

## Libraries List

Libraries are non-daemon modules linked into daemons to provide shared
functionality.

### Core infrastructure

| Module              | Purpose                                                                   |
| ------------------- | ------------------------------------------------------------------------- |
| `ggl-common`        | Nucleus initialization (`ggl_nucleus_init`), version logging              |
| `ggl-constants`     | Nucleus-wide compile-time constants                                       |
| `core-bus`          | Core-bus client and server: RPC call, notify, subscribe over Unix sockets |
| `ggl-socket-server` | Unix socket pool with generational indices; systemd socket activation     |
| `ggipc-auth`        | GG-IPC peer authentication — maps systemd unit PID to component name      |

### Core-bus interface wrappers

Thin client libraries for each daemon's RPC surface.

| Module                  | Purpose                                                                                     |
| ----------------------- | ------------------------------------------------------------------------------------------- |
| `core-bus-gg-config`    | Client wrapper for `gg_config` RPCs (read, write, list, delete, subscribe, backup, restore) |
| `core-bus-aws-iot-mqtt` | Client wrapper for `aws_iot_mqtt` RPCs (publish, subscribe, connection_status)              |
| `core-bus-gghealthd`    | Client wrapper for `gg_health` RPCs                                                         |
| `core-bus-sub-response` | Utility: subscribe and collect the first response synchronously                             |

### Data and serialization

| Module       | Purpose                                                  |
| ------------ | -------------------------------------------------------- |
| `ggl-json`   | JSON Pointer (RFC 6901) parsing into config key paths    |
| `ggl-yaml`   | YAML (libyaml) to `GgObject` conversion                  |
| `ggl-zip`    | libzip wrapper for unpacking component artifact archives |
| `ggl-uri`    | Generic URI parsing and Docker image URI parsing         |
| `ggl-semver` | Semver version comparison and requirement matching       |

### System and process integration

| Module                  | Purpose                                                                |
| ----------------------- | ---------------------------------------------------------------------- |
| `ggl-process`           | Process spawn, call, wait, and kill helpers                            |
| `ggl-binpath`           | Derive sibling binary path from `argv[0]`                              |
| `ggl-proxy-environment` | Reads `networkProxy` config and sets `https_proxy`/`no_proxy` env vars |

### AWS integration

| Module              | Purpose                                                                                 |
| ------------------- | --------------------------------------------------------------------------------------- |
| `ggl-http`          | libcurl wrapper for token fetch, S3 SigV4 download, generic download, ECR auth          |
| `aws-iot-call`      | MQTT request/response pattern with `clientToken` correlation                            |
| `aws-sigv4`         | Wrapper around the AWS SigV4 SDK                                                        |
| `core_mqtt`         | Wrapper around FreeRTOS coreMQTT with project config                                    |
| `ggl-docker-client` | Docker CLI wrapper for pulling, managing, and authenticating Docker-artifact components |

### Recipe handling

| Module        | Purpose                                                                     |
| ------------- | --------------------------------------------------------------------------- |
| `ggl-recipe`  | Parse GG component recipes (YAML to structured map with platform selectors) |
| `recipe2unit` | Generate systemd unit files from component recipes                          |
