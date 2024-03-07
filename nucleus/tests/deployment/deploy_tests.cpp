#include "config/json_deserializer.hpp"
#include "deployment/deployment_model.hpp"
#include "test_tools.hpp"
#include <catch2/catch_all.hpp>

using Catch::Matchers::Equals;

SCENARIO("Json Reader", "[deployment]") {
    auto samples = test::samples();
    GIVEN("An instance of json reader") {
        auto jsonReader = config::JsonDeserializer(scope::context());
        deployment::DeploymentDocument document;
        WHEN("Reading a sample document") {
            jsonReader.read(samples / "basic_document.json");
            jsonReader(document);
            REQUIRE_THAT(document.deploymentId, Equals("cf295b56-9c4c-4fd3-a36b-0bf76e5d7e7c"));
            REQUIRE(document.timestamp == 1708496331538);
            REQUIRE_THAT(document.groupName, Equals("TestGroup"));
            REQUIRE(document.requiredCapabilities.size() == 3);
            REQUIRE_THAT(document.requiredCapabilities[0], Equals("a"));
            REQUIRE_THAT(document.requiredCapabilities[1], Equals("b"));
            REQUIRE_THAT(document.requiredCapabilities[2], Equals("c"));
            REQUIRE(document.componentsToMerge.size() == 1);
            REQUIRE(
                document.componentsToMerge.find("com.example.HelloWorld")
                != document.componentsToMerge.end());
            REQUIRE_THAT(document.componentsToMerge.at("com.example.HelloWorld"), Equals("1.0.0"));
            REQUIRE(
                document.componentsToRemove.find("com.example.HelloWorld")
                != document.componentsToRemove.end());
            REQUIRE_THAT(document.componentsToRemove.at("com.example.HelloWorld"), Equals("0.1.0"));

            REQUIRE_THAT(
                document.configurationArn,
                Equals("arn:123456:configuration:thinggroup/TestGroup:44"));
            REQUIRE_THAT(document.recipeDirectoryPath, Equals("/path/to/recipes"));
            REQUIRE_THAT(document.artifactsDirectoryPath, Equals("/path/to/artifacts"));
            REQUIRE_THAT(document.failureHandlingPolicy, Equals("DO_NOTHING"));
        }
    }
}
