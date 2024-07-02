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
   - [ggdeploymentd-1.1] The deployment service can receive local deployments over IPC CreateLocalDeployment.
   - [ggdeploymentd-1.2] The deployment service can receive local deployments on startup via a deployment doc file.
   - [*ggdeploymentd-1.3] The deployment service can receive IoT shadow
     deployments.
   - [ggdeploymentd-1.4] The deployment service can receive IoT jobs
     deployments.
   - [ggdeploymentd-1.5] Multiple deployments can be received and handled such
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
6. [ggdeploymentd-6] Other components can make a request on the core bus and IPC to get
   the status of a deployment.
7. [ggdeploymentd-7] Other components can make a request on the core bus and IPC to get the list of local deployments.

\* Requirement is of a lower priority

## Core Bus API

Each of the APIs below take a single map as the argument to the call, with the
key-value pairs described by the parameters listed in their respective sections.

### create_local_deployment

The create_local_deployment call adds functionality for the CreateLocalDeployment IPC command. This command creates or updates a local deployment and can specify deployment parameters.

- [ggdeploymentd-bus-createlocaldeployment-1] recipe_directory_path is an optional parameter of type buffer.
  - [ggdeploymentd-bus-createlocaldeployment-1.1] recipe_directory_path is an absolute path to the folder that contains component recipe files.
- [ggdeploymentd-bus-createlocaldeployment-2] artifact_directory_path is an optional parameter of type buffer.
  - [ggdeploymentd-bus-createlocaldeployment-2.1] artifact_directory_path is an absolute path to the folder that contains component artifact files. It must be in the format `/path/to/artifact/folder/component-name/component-version/artifacts`.
- [ggdeploymentd-bus-createlocaldeployment-3] root_component_versions_to_add is an optional parameter of type map.
    - [ggdeploymentd-bus-createlocaldeployment-3.1] root_component_versions_to_add is a map that maps keys of type buffer (component names) to values of type buffer (component versions).
- [ggdeploymentd-bus-createlocaldeployment-4] root_components_to_remove is an optional parameter of type list of buffers.
    - [ggdeploymentd-bus-createlocaldeployment-4.1] root_components_to_remove is a list of component names to uninstall from the device.
- [ggdeploymentd-bus-createlocaldeployment-5] component_to_configuration is an optional parameter of type map.
    - [ggdeploymentd-bus-createlocaldeployment-5.1] component_to_configuration is a map that maps keys of type buffer (component names) to values of type buffer (configuration updates to be made).
      - [ggdeploymentd-bus-createlocaldeployment-5.2] Configuration update values must be in valid JSON format.
- [ggdeploymentd-bus-createlocaldeployment-6] component_to_run_with_info is an optional parameter of type map.
    - [ggdeploymentd-bus-createlocaldeployment-6.1] component_to_run_with_info is a map that maps keys of type buffer (component names) to values of type map (run with info for the component).
      - [ggdeploymentd-bus-createlocaldeployment-6.2] The run with info value map must only include the following keys:
        - [ggdeploymentd-bus-createlocaldeployment-6.3] posix_user is an optional key of type buffer that maps to a value of type buffer (the posix user/group to be used).
        - [ggdeploymentd-bus-createlocaldeployment-6.4] windows_user is an optional key of type buffer that maps to a value of type buffer (the windows user to be used).
        - [ggdeploymentd-bus-createlocaldeployment-6.5] system_resource_limits is an optional key of type buffer that maps to a value of type map (system resource limits information).
          - [ggdeploymentd-bus-createlocaldeployment-6.6] The system resource limits value map must only include the following keys:
            - [ggdeploymentd-bus-createlocaldeployment-6.7] cpus is an optional key of type buffer that maps to a value of type double (maximum cpu usage).
            - [ggdeploymentd-bus-createlocaldeployment-6.8] memory is an optional key of type buffer that maps to a value of type double (RAM in KB).
- [ggdeploymentd-bus-createlocaldeployment-7] group_name is an optional parameter of type buffer.
    - [ggdeploymentd-bus-createlocaldeployment-7.1] group_name is the name of the thing group for the deployment to target.

### *cancel_local_deployment

Currently, cancelling deployments is not in scope of Greengrass Lite. The deployment daemon should acknowledge this IPC request but do nothing with it.

- [ggdeploymentd-bus-cancellocaldeployment-1] deployment_id is a required parameter of type buffer.
  - [ggdeploymentd-bus-cancellocaldeployment-1.1] deployment_id is the ID of the local deployment to cancel.

### get_local_deployment_status

The get_local_deployment_status call adds functionality for the GetLocalDeploymentStatus IPC command. It returns the status of a local deployment.

- [ggdeploymentd-bus-getlocaldeploymentstatus-1] deployment_id is a required parameter of type buffer.
    - [ggdeploymentd-bus-getlocaldeploymentstatus-1.1] deployment_id is the ID of the local deployment to get the status of.

### list_local_deployments

The list_local_deployments call adds functionality for the ListLocalDeployments IPC command. It returns the status of the last 10 local deployments.

- [ggdeploymentd-bus-listlocaldeployments-1] This call has no parameters.

### subscribe_to_component_updates

The subscribe_to_component_updates call adds functionality for the
SubscribeToComponentUpdates IPC command. A component making this call will be
notified before the deployment service updates the component. Components will
not be notified of any updates during a local deployment.

- [ggdeploymentd-bus-subscribetocomponentupdates-1] This call has no parameters.

### defer_component_update

The defer_component_update call adds functionality for the DeferComponentUpdate
IPC command. A component making this call will let the deployment service know
to defer the component update for the specified amount of time.

- [ggdeploymentd-bus-defercomponentupdate-1] deployment_id is a required parameter of type buffer.
    - [ggdeploymentd-bus-defercomponentupdate-1.1] deployment_id is the ID of the local deployment to defer.
- [ggdeploymentd-bus-defercomponentupdate-2] message is an optional parameter of type buffer.
    - [ggdeploymentd-bus-defercomponentupdate-2.1] message is the name of component for which to defer requests. If not provided, it is treated as a default of the component name that made the request.
- [ggdeploymentd-bus-defercomponentupdate-3] recheck_after_ms is an optional parameter of type buffer.
    - [ggdeploymentd-bus-defercomponentupdate-3.1] recheck_after_ms is the amount of time in ms to defer the update. If it is set to 0, then the component acknowledges the update and does not defer. If not provided, it is treated as a default value of 0.

### subscribe_to_validate_configuration_updates

The subscribe_to_validate_configuration_updates call adds functionality for the
SubscribeToValidateConfigurationUpdates IPC command. A component making this
call will be notified before the deployment service updates the component
configuration. Components will not be notified of any configuration changes
during a local deployment.

- [ggdeploymentd-bus-subscribetovalidateconfigurationupdates-1] This call has no parameters.

### send_configuration_validity_report

The send_configuration_validity_report call adds functionality for the
SendConfigurationValidityReport IPC command. A component making this call will
notify the deployment service that the configuration changes is either valid or
invalid.

- [ggdeploymentd-bus-sendconfigurationvalidityreport-1] configuration_validity_report is a required parameter of type map.
    - [ggdeploymentd-bus-sendconfigurationvalidityreport-1.1] configuration_validity_report must only have the following parameters as keys:
      - [ggdeploymentd-bus-sendconfigurationvalidityreport-1.2] status is a required key of type buffer that maps to a value of type buffer.
        - [ggdeploymentd-bus-sendconfigurationvalidityreport-1.3] The value that the status key maps to must be either ACCEPTED or REJECTED.
      - [ggdeploymentd-bus-sendconfigurationvalidityreport-1.4] deployment_id is a required key of type buffer that maps to a value of type buffer that is the ID of the deployment that requested the configuration validation.
      - [ggdeploymentd-bus-sendconfigurationvalidityreport-1.5] message is an optional key of type buffer that maps to a value of type buffer with a message of why the configuration is not valid.