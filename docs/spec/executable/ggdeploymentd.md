# `ggdeploymentd` spec

The Greengrass nucleus lite deployment daemon (`ggdeploymentd`) is a service
that is responsible for receiving and processing deployments. It should be able
to receive deployments from multiple sources and maintain a queue of deployment
tasks.

The deployment daemon will need to receive deployments (can be from a local
deployment, AWS IoT Shadow, or AWS IoT Jobs), placing them into a queue so that
one deployment can be processed at a time. Note that the order that deployments
are received is not guaranteed, for example a group deployment from AWS IoT Jobs
may arrive after another AWS IoT Jobs deployment while having an earlier
timestamp.

A (very) brief overview of the key steps that should be executed during a
deployment task processing:

- Deployment validation: includes verifying that the deployment is not stale,
  checking that any kernel required capabilities are satisfied, and validating
  that all `accessControl` policy `resources` strings in component
  `DefaultConfiguration` and in deployment `configurationUpdate.merge` are
  well-formed.
- Dependency resolution: Resolve the versions of components required by the
  deployment, including getting root components from all thing groups and
  negotiating component version with cloud. This step also gets component
  recipes for the correct component version. Platform attributes (os,
  architecture, architecture.detail, runtime) are sent to the cloud during
  component resolution. If `architecture.detail` is not configured in
  `platformOverride`, it is auto-detected at compile time on ARM platforms.
- Download large configuration: If the deployment configuration is large (>7KB
  for Shadow or >31KB for Jobs), download the full large configuration from
  cloud. Ensure that docker is installed if any component specifies a docker
  artifact.
- Download artifacts: Download artifacts from customer S3, Greengrass service
  accounts S3, or docker. Check that artifacts do not exceed size constraints,
  set permissions, and unarchive if needed.
- Config resolution: Resolve the new configuration to be applied, including
  interpolation for placeholder variables.
- Merge configuration: If configured to do so, notify components that are
  subscribed via SubscribeToComponentUpdates. Wait if there are any
  DeferComponentUpdate responses requesting a delay. Merge the new configuration
  in.
- Track service states: Track the lifecycle states of components and report
  success when all components are RUNNING or FINISHED. Clean up stale versions
  at the end of the deployment process.

A deployment is considered cancellable anytime before the merge configuration
step begins merging the configuration. The Greengrass nucleus lite
implementation may choose to change this to allow cancellation at any step
(TBD).

It is possible for a deployment to require a bootstrap. This means that Nucleus
will need to restart during the deployment. In these deployments, the merge
configuration step will enter a different logic path that keeps track of the
Nucleus state and bootstrap steps.

## Requirements

Currently, cancelled deployments and bootstrap are considered out of scope of
the following requirements.

1. [ggdeploymentd-1] The deployment service is able to receive deployments to be
   handled.
   - [ggdeploymentd-1.1] The deployment service may receive local deployments
     over IPC CreateLocalDeployment.
   - [ggdeploymentd-1.2] The deployment service may receive IoT jobs
     deployments.
   - [ggdeploymentd-1.3] Multiple deployments may be received and handled such
     that they do not conflict with each other.
2. [ggdeploymentd-2] The deployment service may handle a deployment that
   includes new root components.
   - [ggdeploymentd-2.1] The deployment service will resolve component versions
     including pulling in and resolving dependent components not included as
     part of the root component list.
   - [ggdeploymentd-2.2] The deployment service may prepare a component with a
     locally provided recipe.
   - [ggdeploymentd-2.3] The deployment service may prepare a component with a
     recipe from the cloud.
   - [ggdeploymentd-2.4] The deployment service may prepare a component with
     locally provided artifacts.
   - [ggdeploymentd-2.5] The deployment service may prepare a component with an
     artifact from a customer's S3 bucket.
   - [ggdeploymentd-2.6] The deployment service may prepare a component with an
     artifact from a Greengrass service account's S3 bucket.
   - [ggdeploymentd-2.7] The deployment service will run component install
     scripts.
   - [ggdeploymentd-2.8] The deployment service will setup system services for
     components after their install scripts have finished.
   - [ggdeploymentd-2.9] The deployment service will attempt to start components
     only after their dependencies have completed installing and have started.
3. [ggdeploymentd-3] The deployment service fully supports configuration
   features.
   - [ggdeploymentd-3.1] The deployment service may handle a component's default
     configuration.
   - [ggdeploymentd-3.2] The deployment service may handle configuration updates
     and merge/reset configuration from a deployment document.
   - [ggdeploymentd-3.3] The deployment service rejects deployments whose
     `accessControl` policy resources are malformed. See the public docs for
     [IPC authorization policies](https://docs.aws.amazon.com/greengrass/v2/developerguide/interprocess-communication.html#ipc-authorization-policies)
     for the user-facing description of `accessControl` and the supported
     wildcard / escape syntax.
     - [ggdeploymentd-3.3.1] A resource string is malformed if it contains a
       bare `?`, or a `${...}` escape other than `${*}`, `${?}`, or `${$}`.
     - [ggdeploymentd-3.3.2] Both the recipe `DefaultConfiguration` and the
       deployment `configurationUpdate.merge` are checked.
     - [ggdeploymentd-3.3.3] A deployment that contains any malformed
       `accessControl` resource is reported as FAILED, and the malformed
       configuration is not persisted to the config store.
     - [ggdeploymentd-3.3.4] The same well-formedness rule is enforced at IPC
       call time by `ggipcd` against the request resource (the topic being
       published or subscribed to). The shared check lives in the
       `ggl-policy-validation` module so the deploy-time and call-time gates
       cannot drift. See
       [`policy-validation` design](../../design/policy-validation.md).
4. [ggdeploymentd-4] The deployment service is aware of device membership within
   multiple thing groups when executing a new deployment.
   - [ggdeploymentd-4.1] If the device has multiple deployments from different
     thing groups, it will use root components from other deployments during
     resolution.
   - [ggdeploymentd-4.2] Stale components are removed from the device on a new
     deployment handling. A component is considered stale if it was not part of
     the list of resolved component versions.
     - [ggdeploymentd-4.2.1] The names of components that are fully removed
       (i.e. not part of any thing group's resolved component set, so their
       systemd unit, recipe, artifacts, and `services/<name>` config tree are
       deleted) are recorded for the post-deployment fleet status update so the
       cloud can prune them from its inventory. See [ggdeploymentd-5.1.4] below.
5. [ggdeploymentd-5] The deployment service reports outcomes to
   `gg-fleet-statusd` so the cloud reflects the post-deployment state of the
   device.
   - [ggdeploymentd-5.1] After every deployment (including resumed bootstrap
     deployments), the deployment service shall invoke
     `gg_fleet_status/send_fleet_status_update`.
     - [ggdeploymentd-5.1.1] The `trigger` shall be `LOCAL_DEPLOYMENT` for
       deployments of type LOCAL and `THING_GROUP_DEPLOYMENT` for deployments of
       type THING_GROUP. Other deployment types are not currently emitted.
     - [ggdeploymentd-5.1.2] The `deployment_info` map shall include `status`
       (`SUCCEEDED` or `FAILED`), `fleetConfigurationArnForStatus`,
       `deploymentId`, `statusDetails.detailedStatus` (`SUCCESSFUL` or
       `FAILED_ROLLBACK_NOT_REQUESTED`), and `unchangedRootComponents`.
     - [ggdeploymentd-5.1.3] When the deployment did not fully remove any
       components, `removed_components` shall be an empty list.
     - [ggdeploymentd-5.1.4] When stale-component cleanup fully removed one or
       more components, `removed_components` shall contain each removed
       component name (deduplicated). This causes `gg-fleet-statusd` to report
       each name with `status: "UNINSTALLED"` so the cloud prunes them on the
       same `PARTIAL` update, instead of waiting for the next `COMPLETE` update
       (`NUCLEUS_LAUNCH`, `CADENCE`, or `NETWORK_RECONFIGURE`).
     - [ggdeploymentd-5.1.5] When the number of removed components in a single
       deployment exceeds the per-update component cap
       (`GGL_MAX_GENERIC_COMPONENTS`), entries beyond the cap may be dropped
       from the update; the affected components will instead be reconciled by
       the next `COMPLETE` update.
   - [ggdeploymentd-5.2] While a deployment is being processed, the deployment
     service shall suppress event-based fleet status updates so that
     per-component lifecycle transitions caused by the deployment are not
     reported separately. The post-deployment update in [ggdeploymentd-5.1]
     supersedes them.
   - [ggdeploymentd-5.3] Outside of deployments, the deployment service shall
     forward Greengrass component lifecycle state changes to `gg-fleet-statusd`
     with `trigger = COMPONENT_STATUS_CHANGE` so that unexpected restarts,
     failures (`BROKEN`), and similar events are reflected in the cloud without
     waiting for the next `CADENCE` update. Lifecycle changes are sourced from
     `gghealthd`'s broadcast subscription
     `subscribe_to_all_component_state_changes`.

### Future-Looking Possibilities

These are not requirements, but are documented as possible items to be supported
in the future.

- The deployment service may receive AWS IoT Shadow deployments.
- The deployment service may prepare a component with docker artifacts.
- If the deployment configuration is above 7KB for Shadow deployments or 31KB
  for Jobs deployments, the deployment service will download the large
  configuration from the cloud.
- The deployment may be cancelled during certain phases of deployment handling.
- The deployment service may implement additional IPC commands:
  GetLocalDeploymentStatus, ListLocalDeployments, SubscribeToComponentUpdates,
  DeferComponentUpdate, SubscribeToValidateConfigurationUpdates, and
  SendConfigurationValidityReport.
- As part of some of the above IPC commands, the deployment service may allow
  components to defer a deployment or validate configuration changes as part of
  deployment processing.

## Core Bus API

Each of the APIs below take a single map as the argument to the call, with the
key-value pairs described by the parameters listed in their respective sections.

### create_local_deployment

The create_local_deployment call provides functionality equivalent to the
CreateLocalDeployment GG IPC command. This command creates or updates a local
deployment and can specify deployment parameters.

- [ggdeploymentd-bus-createlocaldeployment-1] recipe_directory_path is an
  optional parameter of type buffer.
  - [ggdeploymentd-bus-createlocaldeployment-1.1] recipe_directory_path is an
    absolute path to the folder that contains component recipe files.
- [ggdeploymentd-bus-createlocaldeployment-2] artifact_directory_path is an
  optional parameter of type buffer.
  - [ggdeploymentd-bus-createlocaldeployment-2.1] artifact_directory_path is an
    absolute path to the folder that contains component artifact files. It must
    be in the format
    `/path/to/artifact/folder/component-name/component-version/artifacts`.
- [ggdeploymentd-bus-createlocaldeployment-3] root_component_versions_to_add is
  an optional parameter of type map.
  - [ggdeploymentd-bus-createlocaldeployment-3.1] root_component_versions_to_add
    is a map that maps keys of type buffer (component names) to values of type
    buffer (component versions).
- [ggdeploymentd-bus-createlocaldeployment-4] root_component_versions_to_remove
  is an optional parameter of type list of buffers.
  - [ggdeploymentd-bus-createlocaldeployment-4.1]
    root_component_versions_to_remove is a list of component names to uninstall
    from the device.
- [ggdeploymentd-bus-createlocaldeployment-5] component_to_configuration is an
  optional parameter of type map.
  - [ggdeploymentd-bus-createlocaldeployment-5.1] component_to_configuration is
    a map that maps keys of type buffer (component names) to values of type
    buffer (configuration updates to be made).
    - [ggdeploymentd-bus-createlocaldeployment-5.2] Configuration update values
      must be in valid JSON format.
- [ggdeploymentd-bus-createlocaldeployment-6] group_name is an optional
  parameter of type buffer.
  - [ggdeploymentd-bus-createlocaldeployment-6.1] When provided, the deployment
    targets the specified thing group instead of the default LOCAL_DEPLOYMENTS
    group.
  - [ggdeploymentd-bus-createlocaldeployment-6.2] When empty or not provided,
    the deployment targets the LOCAL_DEPLOYMENTS group.

## Removing Stale Components

As part of deployment processing, stale components will be removed from the
device following a deployment.

- Receive a Map that contains the component_name and version representing
  components to keep
- Received Map will contain the information of all the components and version
  across all thing groups
- Remove any components that does not match the exact component name and version
- Will also support deactivating services related to the component as well as
  unit files, script files, artifacts and recipe files
- When a component is fully removed (its name is not present in the resolved
  component set, i.e. its `services/<name>` config tree, systemd units, and
  recipe/artifact files are deleted), its name is captured and forwarded to
  `gg-fleet-statusd` via the `removed_components` argument of
  `send_fleet_status_update`. `gg-fleet-statusd` then includes the component in
  the published payload with `status: "UNINSTALLED"`, so the cloud prunes it
  from the device's installed-components inventory on the same update.
  Components whose only change is a version bump are not reported as removed,
  even though their old recipe/artifact files on disk are deleted.

- Note: Currently excludes local deployments and might result to removal of all
  those components

## NucleusLite Bootstrap

Greengrass nucleus lite supports bootstrap deployments for the NucleusLite
component.

- Upon receiving a deployment, all deployment info will be stored in the config
  database under services -> DeploymentService -> deploymentState
- Bootstrap scripts of all bootstrap components will be processed and run
- Device will reboot after bootstrap scripts successfully complete
- On reboot, ggdeploymentd will check the config for a previously in progress
  deployment.
- If a deployment is found, it will be resumed and completed. Bootstrap steps
  will be skipped and the remaining lifecycle stages will be processed.
- If a deployment is not found on startup, ggdeploymentd will continue
  functioning as normal and await the next deployment
- At the end of each deployment, all deployment info in the config database will
  be deleted

- Note:
  - Bootstrap is NOT guaranteed to work for components other than NucleusLite.
  - Exit codes in bootstrap scripts are not currently supported. Each script
    will result in the device being rebooted.
  - BootstrapOnRollback is NOT supported

### samples

The expected format of the input map will look as below

```GglMap
## Type of GglMap
{
    "ggl.HelloWorld": "1.0.0",
    "ggl.NewWorld": "2.1.0"
}
```
