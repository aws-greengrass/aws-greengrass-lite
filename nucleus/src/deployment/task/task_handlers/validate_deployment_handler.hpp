#pragma once
#include "lifecycle/kernel.hpp"
#include "task_handler.hpp"


class ValidateDeploymentHandler : public TaskHandler {
public:
    ValidateDeploymentHandler(const scope::UsingContext &context, lifecycle::Kernel &kernel)
        : TaskHandler(context,kernel) {
    }
    deployment::DeploymentResult handleRequest(deployment::Deployment& deployment) override {
        if (!deployment.isCancelled) {
            // TODO: cloud-deployments: Only IoT Jobs can be stale. Ignoring this check for local deployments.

            const auto &kernelSupportedCapabilities = _kernel.getSupportedCapabilities();
            for(const std::string& reqCapability : deployment.deploymentDocumentObj.requiredCapabilities)
            {
                if(!std::count(kernelSupportedCapabilities.begin(), kernelSupportedCapabilities.end(), reqCapability)) {
                    return deployment::DeploymentResult{deployment::DeploymentStatus::FAILED_NO_STATE_CHANGE};
                }
            }
           return this->getNextHandler().handleRequest(deployment); // Pass processing to the next handler
        }
        // TODO: cloud-deployments: Handle cancelled IoT Jobs deployments .
        deployment::DeploymentResult deploymentResult{deployment::DeploymentStatus::FAILED_NO_STATE_CHANGE};
        return deploymentResult;
    }
};