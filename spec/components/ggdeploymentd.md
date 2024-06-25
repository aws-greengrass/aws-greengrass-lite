# `ggdeploymentd` spec

The GG-Lite deployment daemon (`ggdeploymentd`) is a service that is responsible for receiving and processing deployments. It should be able to receive deployments from all possible sources and execute the deployment tasks from a queue.

The deployment daemon will need to receive deployments (can be from: local, IoT shadow, or IoT jobs), placing them into a queue so that one deployment can be processed at a time. Note that the order that deployments are received is not guaranteed, for example a group deployment from IoT Jobs may arrive after another IoT Jobs deployment while having an earlier timestamp.

A (very) brief overview of the key steps that should be executed during a deployment task processing:

* Deployment validation: includes verifying that the deployment is not stale, and checking that any kernel required capabilities are satisfied.
* Dependency resolution: Resolve the versions of components required by the deployment, including getting root components from all thing groups and negotiating component version with cloud. This step also gets component recipes for the correct component version.
* Download large configuration: If the deployment configuration is large (>7KB for Shadow or >31KB for Jobs), download the full large configuration from cloud. Ensure that docker is installed if any component specifies a docker artifact.
* Download artifacts: Download artifacts from customer S3, Greengrass service accounts S3, or docker. Check that artifacts do not exceed size constraints, set permissions, and unarchive if needed.
* Config resolution: Resolve the new configuration to be applied, including interpolation for placeholder variables.
* Merge configuration: If configured to do so, notify components that are subscribed via SubscribeToComponentUpdates. Wait if there are any DeferComponentUpdate responses requesting a delay. Merge the new configuration in.
* Track service states: Track the lifecycle states of components and report success when all components are RUNNING or FINISHED. Clean up stale versions at the end of the deployment process.

A deployment is considered cancellable anytime before the merge configuration step begins merging the configuration. The GG-Lite implementation may choose to change this to allow cancellation at any step (TBD).

It is possible for a deployment to require a bootstrap. This means that Nucleus will need to restart during the deployment. In these deployments, the merge configuration step will enter a different logic path that keeps track of the Nucleus state and bootstrap steps.

## Requirements

1. [ggdeploymentd-1] The deployment service is able to receive deployments to be handled.
   - [ggdeploymentd-1.1] The deployment service can receive local deployments.
   - [ggdeploymentd-1.2] The deployment service can receive IoT shadow deployments.
   - [ggdeploymentd-1.3] The deployment service can receive IoT jobs deployments.
   - [ggdeploymentd-1.4] Multiple deployments can be received and handled such that they do not conflict with each other.
3. [ggdeploymentd-2] The deployment service can handle a deployment that includes new root components.
   - [ggdeploymentd-2.1] The deployment service can resolve a dependency order and versions for components with dependencies.
   - [ggdeploymentd-2.2] The deployment service can prepare a component with a locally provided recipe.
   - [ggdeploymentd-2.3] The deployment service can prepare a component with a recipe from the cloud.