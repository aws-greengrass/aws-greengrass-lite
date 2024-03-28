
#include "deployment/task/default_deployment_task.hpp"
#include <catch2/catch_all.hpp>
#include "test_ggroot.hpp"

// NOLINTBEGIN
SCENARIO("Validate deployment task handler", "[deployment]") {
    test::GGRoot ggRoot;
    GIVEN("A deployment task") {
        ValidateDeploymentHandler validateDeploymentHandler(scope::context(),ggRoot.kernel);

        WHEN("A deployment is cancelled before task is executed") {
            deployment::Deployment deployment{};
            deployment.isCancelled = true;
            deployment::DeploymentResult result = validateDeploymentHandler.handleRequest(deployment);

            THEN("The deployment result is  not successful") {
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
    }
}
// NOLINTEND
