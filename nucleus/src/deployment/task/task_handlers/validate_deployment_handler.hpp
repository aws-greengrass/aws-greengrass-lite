#pragma once

#include "lifecycle/kernel.hpp"
#include "task_handler.hpp"

class ValidateDeploymentHandler : public TaskHandler {
public:
    ValidateDeploymentHandler(const scope::UsingContext &context, lifecycle::Kernel &kernel)
        : TaskHandler(context,kernel) {
    }
    deployment::DeploymentResult handleRequest(deployment::Deployment& deployment) override;

    bool isDeploymentStale(deployment::Deployment &document);
};
