
#include "deployment/model/deployment_queue.hpp"
#include <catch2/catch_all.hpp>

// NOLINTBEGIN
SCENARIO("Operations on a deployment queue", "[deployment]") {
    GIVEN("An empty deployment queue") {
        DeploymentQueue queue{};
        REQUIRE(queue.size() == 0);
        WHEN("Add deployments to the queue") {
            deployment::Deployment deploy{};
            deploy.id = "deployment1";

            deployment::Deployment deploy2{};
            deploy2.id = "deployment2";

            deployment::Deployment deploy3{};
            deploy3.id = "deployment3";

            REQUIRE(queue.offer(deploy));
            REQUIRE(queue.offer(deploy2));
            REQUIRE(queue.offer(deploy3));

            THEN("All unique deployments are added to the queue") {
                REQUIRE(queue.size() == 3);
            }

            THEN("Deployments with same Id are not added to the queue") {
                deployment::Deployment deploy4{};
                deploy4.id = "deployment1"; // Same as 1
                REQUIRE(!queue.offer(deploy4)); // Not added as the id is not unique
            }

            THEN("Deployments are polled in the order of insertion") {
                REQUIRE(queue.poll().id == "deployment1");
                REQUIRE(queue.poll().id == "deployment2");
                REQUIRE(queue.poll().id == "deployment3");
                REQUIRE(queue.size() == 0);
            }
        }
        WHEN(
            "Enqueued deployment is not in DEFAULT stage, then the offered deployment is ignored") {
            deployment::Deployment deploy1{};
            deploy1.id = "deployment1";
            deploy1.deploymentStage = deployment::DeploymentStage::BOOTSTRAP;

            deployment::Deployment deploy2{};
            deploy2.id = "deployment1";
            deploy2.deploymentStage = deployment::DeploymentStage::DEFAULT;

            THEN("Non-default deployment in the queue is not replaced")
            REQUIRE(queue.offer(deploy1));
            REQUIRE(!queue.offer(deploy2));
        }

        WHEN("Offered deployment is cancelled, the enqueued deployment is replaced") {
            deployment::Deployment deploy1{};
            deploy1.id = "deployment1";
            deploy1.deploymentStage = deployment::DeploymentStage::DEFAULT;
            deploy1.deploymentDocument = "oldDeployment";

            deployment::Deployment deploy2{};
            deploy2.id = "deployment1";
            deploy2.isCancelled = true;
            deploy2.deploymentStage = deployment::DeploymentStage::DEFAULT;
            deploy2.deploymentDocument = "newCancelledDeployment";

            THEN("Cancelled deployment replaces the enqueued deployment")
            REQUIRE(queue.offer(deploy1));
            REQUIRE(queue.offer(deploy2));
            REQUIRE(queue.size() == 1);
            auto dep = queue.poll();
            REQUIRE(dep.isCancelled);
            REQUIRE(dep.deploymentDocument == "newCancelledDeployment");
        }
        WHEN("Enqueued deployment is of type SHADOW") {
            deployment::Deployment deploy1{};
            deploy1.id = "deployment1";
            deploy1.deploymentStage = deployment::DeploymentStage::DEFAULT;
            deploy1.deploymentDocument = "oldShadowDeployment";

            deployment::Deployment deploy2{};
            deploy2.id = "deployment1";
            deploy2.deploymentType = deployment::DeploymentType::SHADOW;
            deploy2.deploymentStage = deployment::DeploymentStage::DEFAULT;
            deploy2.deploymentDocument = "newShadowDeployment";

            THEN("Shadow deployment replaces the enqueued deployment")

            REQUIRE(queue.offer(deploy1));
            REQUIRE(queue.offer(deploy2));
            REQUIRE(queue.size() == 1);
            auto dep = queue.poll();
            REQUIRE(dep.deploymentDocument == "newShadowDeployment");
        }
        WHEN("Offered deployment is not in DEFAULT state") {
            deployment::Deployment deploy1{};
            deploy1.id = "deployment1";
            deploy1.deploymentStage = deployment::DeploymentStage::DEFAULT;
            deploy1.deploymentDocument = "oldDeployment";

            deployment::Deployment deploy2{};
            deploy2.id = "deployment1";
            deploy2.deploymentStage = deployment::DeploymentStage::KERNEL_ROLLBACK;
            deploy2.deploymentDocument = "newNonDefaultDeployment";

            THEN("Offered deployment replaces the enqueued deployment")

            REQUIRE(queue.offer(deploy1));
            REQUIRE(queue.offer(deploy2));
            REQUIRE(queue.size() == 1);
            auto dep = queue.poll();
            REQUIRE(dep.deploymentDocument == "newNonDefaultDeployment");
        }
    }
}

// NOLINTEND
