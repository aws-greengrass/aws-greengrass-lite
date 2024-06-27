# `ggdeploymentd` spec

The GG-Lite deployment daemon (`ggdeploymentd`) is a service that is responsible
for receiving and processing deployments. It should be able to receive
deployments from all possible sources and execute the deployment tasks from a
queue.

The deployment daemon will need to receive deployments (can be from: local, IoT
shadow, or IoT jobs), placing them into a queue so that one deployment can be
processed at a time. Note that the order that deployments are received is not
guaranteed, for example a group deployment from IoT Jobs may arrive after
another IoT Jobs deployment while having an earlier timestamp.

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
   - [ggdeploymentd-1.1] The deployment service can receive local deployments.
   - [*ggdeploymentd-1.2] The deployment service can receive IoT shadow
     deployments.
   - [ggdeploymentd-1.3] The deployment service can receive IoT jobs
     deployments.
   - [ggdeploymentd-1.4] Multiple deployments can be received and handled such
     that they do not conflict with each other.
2. [ggdeploymentd-2] The deployment service can handle a deployment that
   includes new root components.
   - [ggdeploymentd-2.1] The deployment service can resolve a dependency order
     and versions for components with dependencies.
   - [ggdeploymentd-2.2] The deployment service can prepare a component with a
     locally provided recipe.
   - [ggdeploymentd-2.3] The deployment service can prepare a component with a
     recipe from the cloud.
   - [ggdeploymentd-2.4] The deployment service can prepare a component with an
     artifact from a customer's S3 bucket.
   - [ggdeploymentd-2.5] The deployment service can prepare a component with an
     artifact from a Greengrass service account's S3 bucket.
   - [*ggdeploymentd-2.6] The deployment service can prepare a component with
     docker artifacts.
3. [ggdeploymentd-3] The deployment service fully supports configuration
   features.
   - [ggdeploymentd-3.1] The deployment service can handle a component's default
     configuration.
   - [ggdeploymentd-3.2] The deployment service can handle configuration updates
     and merge/reset configuration.
   - [*ggdeploymentd-3.3] If the deployment configuration is above 7KB for
     Shadow deployments or 31KB for Jobs deployments, the deployment service
     downloads the large configuration from the cloud.
4. [ggdeploymentd-4] The deployment service is aware of device membership within
   multiple thing groups when executing a new deployment.
   - [ggdeploymentd-4.1] If the device has multiple deployments from different
     thing groups, it will use root components from other deployments during
     resolution.
   - [ggdeploymentd-4.2] Configuration changes from other thing groups is
     correctly handled.
   - [ggdeploymentd-4.3] Stale components are removed from the device on a new
     deployment handling.
5. [ggdeploymentd-5] The deployment service can notify components and get
   confirmation to move forward with the deployment.
   - [ggdeploymentd-5.1] The SubscribeToComponentUpdates IPC command is
     supported and the deployment service notifies components about updates if
     configured to do so.
   - [ggdeploymentd-5.2] The DeferComponentUpdates IPC command is supported and
     the deployment service defers a component update if notified.
   - [ggdeploymentd-5.3] The SubscribeToValidateConfigurationUpdates IPC command
     is supported and the deployment service notifies components about updates
     to the component configuration.
   - [ggdeploymentd-5.4] The SendConfigurationValidityReport IPC command is
     supported and the deployment fails if a component notifies that the
     configuration is not valid.
6. [ggdeploymentd-6] Other components can make a request on the core bus to get
   the status of a deployment.

\* Requirement is of a lower priority

## Core Bus API (WIP)

Each of the APIs below take a single map as the argument to the call, with the
key-value pairs described by the parameters listed in their respective sections.

### create_local_deployment

### cancel_local_deployment

### get_local_deployment_status

### list_local_deployments

### subscribe_to_component_updates

The subscribe_to_component_updates call adds functionality for the
SubscribeToComponentUpdates IPC command. A component making this call will be
notified before the deployment service updates the component. Components will
not be notified of any updates during a local deployment.

### defer_component_update

The defer_component_update call adds functionality for the DeferComponentUpdate
IPC command. A component making this call will let the deployment service know
to defer the component update for the specified amount of time.

### subscribe_to_validate_configuration_updates

The subscribe_to_validate_configuration_updates call adds functionality for the
SubscribeToValidateConfigurationUpdates IPC command. A component making this
call will be notified before the deployment service updates the component
configuration. Components will not be notified of any configuration changes
during a local deployment.

### send_configuration_validity_report

The send_configuration_validity_report call adds functionality for the
SendConfigurationValidityReport IPC command. A component making this call will
notify the deployment service that the configuration changes is either valid or
invalid.
