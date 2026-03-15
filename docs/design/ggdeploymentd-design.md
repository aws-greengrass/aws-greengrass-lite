# `ggdeploymentd` Design

See [`ggdeploymentd` spec](../spec/executable/ggdeploymentd.md) for the public
interface.

## Overview

The deployment daemon (`ggdeploymentd`) receives, queues, and executes
component deployments. It supports both local deployments (via IPC or deployment
doc files) and cloud deployments (via AWS IoT Jobs). The daemon translates
deployment documents into installed, configured, and running systemd services.

## Requirements

The following spec requirements are implemented:

- [ggdeploymentd-1.1] Local deployments received over IPC
  (`CreateLocalDeployment`).
- [ggdeploymentd-1.2] Cloud deployments received via AWS IoT Jobs.
- [ggdeploymentd-1.3] Multiple deployments queued and deduplicated by
  deployment ID.
- [ggdeploymentd-2.1] Dependency version resolution — cloud-side via
  `ResolveComponentCandidates`, local via semver range matching (partial: no
  full dependency graph or cycle detection).
- [ggdeploymentd-2.2] Component preparation with locally provided recipes.
- [ggdeploymentd-2.3] Component preparation with cloud-resolved recipes.
- [ggdeploymentd-2.4] Component preparation with locally provided artifacts.
- [ggdeploymentd-2.5] Artifact download from customer S3 buckets (SigV4 + TES).
- [ggdeploymentd-2.6] Artifact download from Greengrass service S3 buckets.
- [ggdeploymentd-2.7] Component install scripts executed via systemd.
- [ggdeploymentd-2.8] System services set up after install scripts complete.
- [ggdeploymentd-2.9] Components started after dependencies have installed and
  started.
- [ggdeploymentd-3.1] Default configuration from recipe applied.
- [ggdeploymentd-3.2] Configuration merge and reset from deployment document
  via `ggconfigd`.
- [ggdeploymentd-4.1] Root components from other thing group deployments used
  during resolution.
- [ggdeploymentd-4.2] Stale components removed after deployment.

Not yet implemented:

- Artifact permission application (`Permission.Read`, `Permission.Execute`) —
  no spec requirement; parity gap with Greengrass Nucleus.
- `shutdown` and `recover` lifecycle phases — no spec requirement; parity gap
  with Greengrass Nucleus (see `docs/RECIPE_SUPPORT_CHANGES.md`).
- Full component rollback on deployment failure — no spec requirement; config
  rollback is implemented for endpoint switch deployments only.

## Architecture

On startup, `ggdeploymentd` launches three concurrent activities:

1. **IoT Jobs listener thread** (`iot_jobs_listener.c`) — subscribes to AWS IoT
   Jobs MQTT topics via `iotcored`, receives cloud deployment documents, and
   enqueues them. Reports deployment status (IN_PROGRESS / SUCCEEDED / FAILED)
   back to IoT Jobs. Includes retry with backoff on MQTT failures.

2. **Deployment handler thread** (`deployment_handler.c`) — dequeues deployments
   one at a time and executes them through the full deployment pipeline (see
   System Flow below). On startup, checks for and resumes any in-progress
   bootstrap deployment.

3. **Core bus server** (`bus_server.c`) — listens for `CreateLocalDeployment`
   IPC requests and enqueues them as local deployments.

### Module Breakdown

| Module | Responsibility |
|:---|:---|
| `entry.c` | Reads `rootPath` from config, spawns threads, starts bus server |
| `deployment_queue.c` | Thread-safe deployment queue with blocking dequeue. Deduplicates by deployment ID |
| `deployment_handler.c` | Main deployment pipeline — dependency resolution, artifact download, config merge, systemd orchestration, status reporting |
| `deployment_model.h` | Data types: `GglDeployment`, `DeploymentContext`, `PhaseSelection`, `GglDeploymentType` |
| `iot_jobs_listener.c` | MQTT subscription to IoT Jobs topics, deployment document parsing, job status updates with retry/backoff |
| `component_store.c` | Recipe directory operations — iterating components, finding available versions via semver matching |
| `component_manager.c` | Version resolution — resolves a version requirement against locally available component versions |
| `component_config.c` | Applies configuration updates (RESET and MERGE operations) to component config via `ggconfigd` |
| `bootstrap_manager.c` | Persists deployment state to config for bootstrap resume. Tracks per-component bootstrap/completion status. Processes bootstrap lifecycle phase |
| `stale_component.c` | Disables/unlinks old systemd services and cleans up stale component versions after deployment |
| `credential_endpoint_validation.c` | Validates IoT data endpoint format for endpoint switch deployments |
| `iotcored_instance.c` | Manages secondary `iotcored` instances for endpoint switch verification |
| `priv_io.c` | Privileged I/O helper for operations requiring root |

## System Flow

### Deployment Sources

- **Local deployments**: Received via `CreateLocalDeployment` IPC or deployment
  doc files on startup. Recipes and artifacts are provided on-disk.
- **Cloud deployments**: Received via IoT Jobs. The jobs listener subscribes to
  `$aws/things/{thingName}/jobs/notify` and fetches job documents from
  `$aws/things/{thingName}/jobs/$next/get`. Recipes are resolved via
  `ResolveComponentCandidates` (Greengrass data plane) and artifacts are
  downloaded from S3.

### Deployment Pipeline

When a deployment is dequeued, `handle_deployment` executes the following steps:

1. **Endpoint switch validation** — If the deployment modifies the IoT data
   endpoint (NucleusLite config), validates the new endpoint format and persists
   rollback state before proceeding.

2. **Config backup** — Takes a config snapshot via `ggl_gg_config_backup()` for
   rollback on failure. Skipped during bootstrap resume (backup already exists).

3. **Copy local recipes/artifacts** — For local deployments, copies recipes and
   artifacts from the provided paths into the component store
   (`packages/recipes/` and `packages/artifacts/`).

4. **Dependency resolution** — Resolves the full set of components to deploy:
   - For cloud deployments: calls `ResolveComponentCandidates` via the
     Greengrass data plane with SigV4-signed requests. Fetches device thing
     groups and resolves cross-group conflicts.
   - For local deployments: resolves dependencies locally using recipes in the
     component store and semver range matching (`is_in_range()` via
     `ggl-semver`). Note: this does not build a full dependency graph or
     perform cycle detection — it is a simpler resolution compared to
     Greengrass Nucleus.

5. **Per-component processing** — For each resolved component:
   - Skips components already completed in a previous run (bootstrap resume).
   - Retrieves recipe from the component store.
   - Downloads artifacts from S3 using SigV4 + TES credentials with retry.
     Verifies SHA-256 digests. Unarchives ZIP artifacts.
   - Writes component version and configuration ARN to `ggconfigd`.
   - Applies config RESET, writes `DefaultConfiguration` from recipe, then
     applies config MERGE from the deployment document.
   - Converts recipe to systemd unit files via `recipe2unit`.
   - Tracks which components are new/updated vs. already running.

6. **Bootstrap phase** (partial) — Processes components with bootstrap lifecycle
   steps. Saves deployment state to config, runs bootstrap services, and waits
   for completion. If a bootstrap requires a nucleus restart, state is persisted
   so the deployment resumes after reboot. See Known Limitations.

7. **Install phase** — Links and starts install service files
   (`ggl.<component>.install.service`) via `systemctl`. Waits for all install
   phases to complete via `gghealthd` lifecycle status subscription.

8. **Run/Startup phase** — Links, enables, and starts run service files
   (`ggl.<component>.service`) via `systemctl`. Saves each component as
   "completed" for bootstrap tracking.

9. **Systemd activation** — Runs `systemctl daemon-reload`, `reset-failed`, and
   `start greengrass-lite.target` to activate all components.

10. **Wait for health** — Subscribes to `gghealthd`
    `subscribe_to_lifecycle_completion` and waits for all components to report
    RUNNING or FINISHED status.

11. **Stale component cleanup** — Removes old versions of components that are no
    longer active: disables/unlinks their systemd services, deletes recipes,
    artifacts, and config entries.

12. **Endpoint switch verification** — For endpoint switch deployments, verifies
    MQTT reconnection to the new IoT data endpoint. Rolls back config on
    failure.

### Post-Deployment

After `handle_deployment` returns:

- **Fleet status update** — Sends a fleet status update via `gg-fleet-statusd`
  with deployment result details.
- **IoT Jobs status** — Reports SUCCEEDED or FAILED to IoT Jobs. For endpoint
  switch deployments, reports to the source endpoint's iotcored instance if the
  switch failed.
- **Config rollback** — If the deployment failed and involved an endpoint
  switch, restores the config snapshot via `ggl_gg_config_restore()`.
- **Cleanup** — Deletes saved deployment state from config and releases the
  deployment from the queue.

## Deployment Types

| Type | Source | Recipe Resolution | Artifact Resolution |
|:---|:---|:---|:---|
| Local | IPC or doc file | On-disk recipes in provided path | On-disk artifacts or S3 download if ARN present |
| Cloud (Thing Group) | IoT Jobs | `ResolveComponentCandidates` API | S3 download with SigV4 + TES credentials |

## Configuration

The daemon reads the following from `ggconfigd`:

- `system.rootPath` — Root path for all Greengrass data
- `system.thingName` — Device thing name
- `system.certificateFilePath`, `system.privateKeyPath`, `system.rootCaPath` —
  TLS credentials for data plane calls
- `services.aws.greengrass.NucleusLite.configuration.iotDataEndpoint` — IoT
  Core data endpoint
- `services.aws.greengrass.NucleusLite.configuration.greengrassDataPlanePort` —
  Data plane port
- `services.aws.greengrass.NucleusLite.configuration.awsRegion` — AWS region
- `services.aws.greengrass.NucleusLite.configuration.runWithDefault.posixUser`
  — User:group for component processes

## Known Limitations

- **Artifact permissions**: Recipes specify `Permission.Read` and
  `Permission.Execute` but these are not applied after download (see `TODO` in
  `deployment_handler.c`).
- **Bootstrap**: Partially supported (see `docs/RECIPE_SUPPORT_CHANGES.md`).
- **Shutdown/recover phases**: Not supported. See
  `docs/RECIPE_SUPPORT_CHANGES.md`.
- **DeferComponentUpdate**: Not supported — components cannot defer an
  in-progress deployment.
- **Rollback**: Config rollback is implemented for endpoint switch deployments.
  Full component rollback (reverting unit files, artifacts, and component
  versions on failure) is not yet implemented.
- **Dependency resolution**: Local resolution uses semver range matching only.
  Full dependency graph construction and cycle detection are not implemented.
