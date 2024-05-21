#include "validate_deployment_handler.hpp"
#include "scope/context_full.hpp"


bool ValidateDeploymentHandler::isDeploymentStale(deployment::Deployment &document) {
    if(document.deploymentType != deployment::DeploymentType::IOT_JOBS || document.deploymentDocumentObj.groupName.empty()) {
        // Then it is not a group deployment, which is not stale
        return false;
    }

    auto lastDeployment = _kernel.getConfig().lookupTopics(
            {"services", "DeploymentService", "GroupToLastDeployment", document.deploymentDocumentObj.groupName}
            );

    std::optional<config::Topic> lastDeploymentTimestampOptional = lastDeployment->find({"timestamp"});

    if(!lastDeploymentTimestampOptional.has_value() || lastDeploymentTimestampOptional->getInt() == 0) {
        // Must be a new deployment, since we do not have previous deployment data
        return false;
    }

    // Deployment is stale if the timestamp is less than the timestamp of the last processed deployment.
    return document.deploymentDocumentObj.timestamp < lastDeploymentTimestampOptional->getInt();
}

deployment::DeploymentResult ValidateDeploymentHandler::handleRequest(deployment::Deployment &deployment) {
    if (!deployment.isCancelled) {
        if(isDeploymentStale(deployment)) {
            return deployment::DeploymentResult{deployment::DeploymentStatus::FAILED_NO_STATE_CHANGE};
        }
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