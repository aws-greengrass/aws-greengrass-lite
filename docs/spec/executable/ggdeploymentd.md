# `ggdeploymentd` spec

The GG-Lite deployment daemon (`ggdeploymentd`) is a service that is responsible
for receiving and processing deployments. It should be able to receive
deployments from multiple sources and maintain a queue of deployment tasks.

The deployment daemon will need to receive deployments (can be from a local
deployment, AWS IoT Shadow, or AWS IoT Jobs), placing them into a queue so that
one deployment can be processed at a time. Note that the order that deployments
are received is not guaranteed, for example a group deployment from AWS IoT Jobs
may arrive after another AWS IoT Jobs deployment while having an earlier
timestamp.

A (very) brief overview of the key steps that should be executed during a
deployment task processing:

- Deployment validation: includes verifying that the deployment is not stale,
  and checking that any kernel required capabilities are satisfied.
- Dependency resolution: Resolve the versions of components required by the
  deployment, including getting root components from all thing groups and
  negotiating component version with cloud. This step also gets component
  recipes for the correct component version.
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
step begins merging the configuration. The GG-Lite implementation may choose to
change this to allow cancellation at any step (TBD).

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
4. [ggdeploymentd-4] The deployment service is aware of device membership within
   multiple thing groups when executing a new deployment.
   - [ggdeploymentd-4.1] If the device has multiple deployments from different
     thing groups, it will use root components from other deployments during
     resolution.
   - [ggdeploymentd-4.2] Stale components are removed from the device on a new
     deployment handling. A component is considered stale if it was not part of
     the list of resolved component versions.

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
- [ggdeploymentd-bus-createlocaldeployment-4] root_components_to_remove is an
  optional parameter of type list of buffers.
  - [ggdeploymentd-bus-createlocaldeployment-4.1] root_components_to_remove is a
    list of component names to uninstall from the device.
- [ggdeploymentd-bus-createlocaldeployment-5] component_to_configuration is an
  optional parameter of type map.
  - [ggdeploymentd-bus-createlocaldeployment-5.1] component_to_configuration is
    a map that maps keys of type buffer (component names) to values of type
    buffer (configuration updates to be made).
    - [ggdeploymentd-bus-createlocaldeployment-5.2] Configuration update values
      must be in valid JSON format.

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

- Note: Currently excludes local deployments and might result to removal of all
  those components

## NucleusLite Bootstrap

GG-Lite supports bootstrap deployments for the NucleusLite component.

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
