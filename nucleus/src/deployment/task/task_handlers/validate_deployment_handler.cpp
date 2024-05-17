#include "validate_deployment_handler.hpp"
#include "scope/context_full.hpp"

bool ValidateDeploymentHandler::isDeploymentStale(deployment::Deployment &document, ) {
    if(document.deploymentType != deployment::DeploymentType::IOT_JOBS || document.deploymentDocumentObj.groupName.empty()) {
        // Then it is not a group deployment, which is not stale
        return false;
    }

    auto groupToLastDeploymentMap = _kernel.getConfig().lookupTopics({"services", "DeploymentService", "GroupToLastDeployment"});

    auto lastDeployment = groupToLastDeploymentMap->lookup({document.deploymentDocumentObj.groupName}).getStruct();
}

deployment::DeploymentResult ValidateDeploymentHandler::handleRequest(deployment::Deployment &deployment) {
    if (!deployment.isCancelled) {
        // TODO: cloud-deployments: Only IoT Jobs can be stale. Ignoring this check for local deployments.
        std::vector<std::string> kernelSupportedCapabilities = _kernel.getSupportedCapabilities();
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