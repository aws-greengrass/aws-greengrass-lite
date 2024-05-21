
#include "deployment/task/default_deployment_task.hpp"
#include <catch2/catch_all.hpp>
#include "test_ggroot.hpp"


// For testing, instead of passing to the next handler, pass to a dummy handler that will return success state
class DummySuccessHandler : public TaskHandler  {
public:
    DummySuccessHandler(const scope::UsingContext &context, lifecycle::Kernel &kernel)
            : TaskHandler(context,kernel) {
    }
    deployment::DeploymentResult handleRequest(deployment::Deployment& deployment) override {
        return deployment::DeploymentResult{deployment::DeploymentStatus::SUCCESSFUL};
    }
};

// NOLINTBEGIN
SCENARIO("Validate deployment task handler", "[deployment]") {
    test::GGRoot ggRoot;
    GIVEN("A deployment task") {
        ValidateDeploymentHandler validateDeploymentHandler(scope::context(),ggRoot.kernel);
        DummySuccessHandler dummySuccessHandler(scope::context(),ggRoot.kernel);
        validateDeploymentHandler.setNextHandler(dummySuccessHandler);

        WHEN("A deployment is cancelled before task is executed") {
            deployment::Deployment deployment{};
            deployment.isCancelled = true;
            deployment::DeploymentResult result = validateDeploymentHandler.handleRequest(deployment);

            THEN("The deployment result is not successful") {
                REQUIRE(result.deploymentStatus == deployment::DeploymentStatus::FAILED_NO_STATE_CHANGE);
            }
        }

        WHEN("A deployment is created with a capability that is not supported by kernel") {
            deployment::Deployment deployment{};
            deployment.deploymentDocumentObj.requiredCapabilities = std::vector<std::string>{"NOT_SUPPORTED"};
            deployment::DeploymentResult result = validateDeploymentHandler.handleRequest(deployment);
            THEN("The deployment result is not successful") {
                REQUIRE(result.deploymentStatus == deployment::DeploymentStatus::FAILED_NO_STATE_CHANGE);
            }
        }

        WHEN("A group deployment that is not stale, not cancelled, and supported by kernel is handled") {
            deployment::Deployment deployment{};
            deployment.deploymentDocumentObj.timestamp = 100;
            deployment.deploymentDocumentObj.groupName = "Group";
            deployment.deploymentType = deployment::DeploymentType::IOT_JOBS;
            deployment::DeploymentResult result = validateDeploymentHandler.handleRequest(deployment);
            THEN("The deployment result is successful") {
                REQUIRE(result.deploymentStatus == deployment::DeploymentStatus::SUCCESSFUL);
            }
        }
    }
}
// NOLINTEND
