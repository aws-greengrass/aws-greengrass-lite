#include "default_deployment_task.hpp"

void DefaultDeploymentTask::handleTaskExecution(deployment::Deployment &deployment) {
    // Assume that deployment obj exists already

    // Build the chain of handlers
    validateDeploymentHandler.setNextHandler(resolveDependenciesHandler);
    resolveDependenciesHandler.setNextHandler(prepareArtifactsHandler);
    prepareArtifactsHandler.setNextHandler(kernelConfigResolveHandler);
    kernelConfigResolveHandler.setNextHandler(mergeConfigHandler);
    mergeConfigHandler.setNextHandler(trackServiceStateHandler);
    // add rollback handler if needed.

    // Invoke the first handler
    validateDeploymentHandler.handleRequest(deployment);
    // TODO: Persist and publish deployment status
}
