#pragma once
#include "deployment/task/task_handlers/validate_deployment_handler.hpp"
#include "deployment/task/task_handlers/resolve_dependencies_handler.hpp"
#include "deployment/task/task_handlers/resolve_config_handler.hpp"
#include "deployment/task/task_handlers/prepare_artifacts_handler.hpp"
#include "deployment/task/task_handlers/merge_config_handler.hpp"
#include "deployment/task/task_handlers/track_service_status_handler.hpp"

class DefaultDeploymentTask {
private:

    ValidateDeploymentHandler validateDeploymentHandler;
    ResolveDependenciesHandler resolveDependenciesHandler;
    PrepareArtifactsHandler prepareArtifactsHandler;
    KernelConfigResolveHandler kernelConfigResolveHandler;
    MergeConfigHandler mergeConfigHandler;
    TrackServiceStateHandler trackServiceStateHandler;
public:
    void handleTaskExecution(deployment::Deployment &deployment);
};