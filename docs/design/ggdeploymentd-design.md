# GG-Lite `ggdeploymentd`: Initial Implementation Design

## Overview

The GG-Lite deployment daemon (`ggdeploymentd`) is a service that is responsible
for receiving and processing deployments. It should be able to receive
deployments from all possible sources and execute the deployment tasks from a
queue.

As an initial goal toward complete deployment implementation, a subset of deployments is being considered for this document. The user story for this functionality should be that a user can create a local deployment with a single "Hello World" component.

## Requirements

Out of the larger scope of requirements set in the `ggdeploymentd` spec document, the following are included in the initial implementation scope:
- [ggdeploymentd-1.1] The deployment service can receive local deployments over IPC CreateLocalDeployment.
- [ggdeploymentd-1.2] The deployment service can receive local deployments on startup via a deployment doc file.
- [ggdeploymentd-2.2] The deployment service can prepare a component with a
  locally provided recipe.

Note that this does not include a full set of component features, such as component configuration or component artifacts.

## System Flow

Receiving Local Deployments:
1. On start, the `ggdeploymentd` daemon checks for any provided deployment doc files, forms them into deployment tasks, and adds these to the deployment queue.
2. The daemon receives CreateLocalDeployment IPC requests that includes the deployment doc.
2. CreateLocalDeployment requests are formed into a deployment task to add to the deployment queue. If it is an update to an existing deployment in queue, update that deployment instead.

Executing Deployments:
1. If the deployment queue is not empty, then take one deployment task from the queue to be executed.
2. Copy recipe from the specified recipe path to the Greengrass-managed packages folder.
3. Notify orchestrator that components are ready.
4. When component health status is good, notify FSS that the deployment is successful. Deployment logic is complete and starts again at step 1.

