#include "deployment/recipe_loader.hpp"
#include "test_tools.hpp"
#include <catch2/catch_all.hpp>

using Catch::Matchers::Equals;

SCENARIO("Recipe Reader", "[deployment]") {
    auto samples = test::samples();
    GIVEN("An instance of recipe reader") {
        auto yaml_reader = deployment::RecipeLoader();
        WHEN("Reading a hello world recipe") {
            auto recipe = yaml_reader.read(samples / "hello_recipe.yml");
            THEN("The recipe is read") {
                REQUIRE_THAT(recipe.formatVersion, Equals("2020-01-25"));
                REQUIRE_THAT(recipe.componentName, Equals("com.example.HelloWorld"));
                REQUIRE_THAT(recipe.componentVersion, Equals("1.0.0"));
                REQUIRE_THAT(
                    recipe.componentDescription, Equals("My first AWS IoT Greengrass component."));
                REQUIRE_THAT(recipe.componentPublisher, Equals("Amazon"));

                REQUIRE(recipe.configuration.defaultConfiguration->hasKey("Message"));
                REQUIRE_THAT(
                    recipe.configuration.defaultConfiguration->get("Message").getString(),
                    Equals("world"));

                REQUIRE(recipe.manifests.size() == 3);

                REQUIRE(recipe.manifests[0].platform.os == "linux");
                REQUIRE(!recipe.manifests[0].lifecycle.empty());
                REQUIRE(recipe.manifests[0].lifecycle.size() == 1);
                REQUIRE(
                    recipe.manifests[0].lifecycle.find("run")
                    != recipe.manifests[0].lifecycle.end());
                REQUIRE(recipe.manifests[0].lifecycle.at("run").isScalar());

                REQUIRE(
                    recipe.manifests[0].lifecycle.at("run").getString()
                    == "python3 -u {artifacts:path}/hello_world.py \"{configuration:/Message}\"\n");

                REQUIRE(recipe.manifests[1].platform.os == "darwin");
                REQUIRE(!recipe.manifests[1].lifecycle.empty());
                REQUIRE(recipe.manifests[1].lifecycle.size() == 1);
                REQUIRE(
                    recipe.manifests[1].lifecycle.find("run")
                    != recipe.manifests[1].lifecycle.end());
                REQUIRE(recipe.manifests[1].lifecycle.at("run").isScalar());
                REQUIRE(
                    recipe.manifests[1].lifecycle.at("run").getString()
                    == "python3 -u {artifacts:path}/hello_world.py \"{configuration:/Message}\"\n");

                REQUIRE(recipe.manifests[2].platform.os == "windows");
                REQUIRE(!recipe.manifests[2].lifecycle.empty());
                REQUIRE(recipe.manifests[2].lifecycle.size() == 1);
                REQUIRE(
                    recipe.manifests[2].lifecycle.find("run")
                    != recipe.manifests[2].lifecycle.end());
                REQUIRE(recipe.manifests[2].lifecycle.at("run").isScalar());
                REQUIRE(
                    recipe.manifests[2].lifecycle.at("run").getString()
                    == "py -3 -u {artifacts:path}/hello_world.py \"{configuration:/Message}\"\n");
            }
        }
        WHEN("Reading a recipe with dependencies") {
            auto recipe = yaml_reader.read(samples / "sample1.yaml");
            THEN("The recipe is read") {
                REQUIRE_THAT(recipe.formatVersion, Equals("2020-01-25"));
                REQUIRE_THAT(recipe.componentName, Equals("com.example.HelloWorld"));
                REQUIRE_THAT(recipe.componentVersion, Equals("1.0.0"));

                REQUIRE(recipe.manifests.size() == 2);

                REQUIRE(recipe.manifests[0].platform.os == "linux");
                REQUIRE(recipe.manifests[0].platform.architecture == "amd64");

                REQUIRE(!recipe.manifests[0].lifecycle.empty());
                REQUIRE(recipe.manifests[0].lifecycle.size() == 2);
                REQUIRE(
                    recipe.manifests[0].lifecycle.find("install")
                    != recipe.manifests[0].lifecycle.end());
                REQUIRE(recipe.manifests[0].lifecycle.at("install").isScalar());
                REQUIRE_THAT(
                    recipe.manifests[0].lifecycle.at("install").getString(), Equals("echo Hello"));

                REQUIRE(
                    recipe.manifests[0].lifecycle.find("run")
                    != recipe.manifests[0].lifecycle.end());
                REQUIRE(recipe.manifests[0].lifecycle.at("run").isScalar());
                REQUIRE_THAT(
                    recipe.manifests[0].lifecycle.at("run").getString(),
                    Equals("apt-get update\napt-get install python3.7\n"));

                REQUIRE(recipe.manifests[1].platform.os == "windows");
                REQUIRE(recipe.manifests[1].platform.architecture == "amd64");

                REQUIRE(recipe.componentDependencies.size() == 2);
                REQUIRE(
                    recipe.componentDependencies.find("aws.greengrass.tokenexchangeservice")
                    != recipe.componentDependencies.end());
                REQUIRE(
                    recipe.componentDependencies.find("aws.greengrass.s3service")
                    != recipe.componentDependencies.end());
                REQUIRE_THAT(
                    recipe.componentDependencies.at("aws.greengrass.tokenexchangeservice")
                        .versionRequirement,
                    Equals("^2.0.0"));
                REQUIRE_THAT(
                    recipe.componentDependencies.at("aws.greengrass.tokenexchangeservice")
                        .dependencyType,
                    Equals("HARD"));
                REQUIRE_THAT(
                    recipe.componentDependencies.at("aws.greengrass.s3service").versionRequirement,
                    Equals("^3.0.0"));
                REQUIRE_THAT(
                    recipe.componentDependencies.at("aws.greengrass.s3service").dependencyType,
                    Equals("SOFT"));
                REQUIRE(recipe.configuration.defaultConfiguration->hasKey("Message"));
                REQUIRE_THAT(
                    recipe.configuration.defaultConfiguration->get("Message").getString(),
                    Equals("Hello World!"));
            }
        }
        WHEN("Reading a recipe with artifacts") {
            auto recipe = yaml_reader.read(samples / "plugin_recipe.yaml");
            THEN("The recipe is read") {
                REQUIRE_THAT(recipe.getFormatVersion(), Equals("2020-01-25"));
                REQUIRE_THAT(recipe.getComponentName(), Equals("aws.greengrass.some-plugin"));
                REQUIRE_THAT(recipe.getComponentDescription(), Equals("Just a plugin"));
                REQUIRE_THAT(recipe.getComponentPublisher(), Equals("Me"));
                REQUIRE_THAT(recipe.getComponentVersion(), Equals("1.1.0"));
                REQUIRE(recipe.componentType == "aws.greengrass.plugin");

                REQUIRE(recipe.getManifests().size() == 3);

                REQUIRE(recipe.getManifests()[0].platform.os == "all");
                REQUIRE(recipe.getManifests()[0].platform.nucleusType == "java");
                REQUIRE(recipe.getManifests()[0].artifacts.size() == 2);
                REQUIRE(
                    recipe.getManifests()[0].artifacts[0].uri
                    == "s3://mock-bucket/java/plugin.jar");
                REQUIRE(
                    recipe.getManifests()[0].artifacts[1].uri
                    == "s3://mock-bucket/shared/bundle.zip");

                REQUIRE(recipe.getManifests()[1].platform.os == "linux");
                REQUIRE(recipe.getManifests()[1].platform.nucleusType == "lite");
                REQUIRE(recipe.getManifests()[1].platform.architecture == "aarch64");
                REQUIRE(recipe.getManifests()[1].artifacts.size() == 2);
                REQUIRE(
                    recipe.getManifests()[1].artifacts[0].uri
                    == "s3://mock-bucket/aarch64/plugin.so");
                REQUIRE(
                    recipe.getManifests()[1].artifacts[1].uri
                    == "s3://mock-bucket/shared/bundle.zip");

                REQUIRE(recipe.getManifests()[2].platform.os == "linux");
                REQUIRE(recipe.getManifests()[2].platform.nucleusType == "lite");
                REQUIRE(recipe.getManifests()[2].platform.architecture == "amd64");
                REQUIRE(recipe.getManifests()[2].artifacts.size() == 2);
                REQUIRE(
                    recipe.getManifests()[2].artifacts[0].uri
                    == "s3://mock-bucket/amd64/plugin.so");
                REQUIRE(
                    recipe.getManifests()[2].artifacts[1].uri
                    == "s3://mock-bucket/shared/bundle.zip");
            }
        }

        WHEN("Reading a recipe with selections") {
            auto recipe = yaml_reader.read(samples / "selection_recipe.yml");
            THEN("The recipe is read") {
                REQUIRE_THAT(recipe.formatVersion, Equals("2020-01-25"));
                REQUIRE_THAT(recipe.componentName, Equals("com.example.HelloWorld"));
                REQUIRE_THAT(recipe.componentVersion, Equals("1.0.0"));
                REQUIRE_THAT(
                    recipe.componentDescription, Equals("My first AWS IoT Greengrass component."));
                REQUIRE_THAT(recipe.componentPublisher, Equals("Amazon"));

                REQUIRE(recipe.configuration.defaultConfiguration->hasKey("Message"));
                REQUIRE_THAT(
                    recipe.configuration.defaultConfiguration->get("Message").getString(),
                    Equals("world"));

                REQUIRE(recipe.manifests.size() == 1);

                REQUIRE(recipe.manifests[0].platform.os == "linux");

                REQUIRE(!recipe.manifests[0].lifecycle.empty());
                REQUIRE(recipe.manifests[0].lifecycle.size() == 1);
                REQUIRE(
                    recipe.manifests[0].lifecycle.find("run")
                    != recipe.manifests[0].lifecycle.end());
                REQUIRE(recipe.manifests[0].lifecycle.at("run").isScalar());
                REQUIRE(
                    recipe.manifests[0].lifecycle.at("run").getString()
                    == "python3 -u {artifacts:path}/hello_world.py \"{configuration:/Message}\"\n");

                REQUIRE(recipe.manifests[0].selections.size() == 2);
                REQUIRE_THAT(recipe.manifests[0].selections[0], Equals("key1"));
                REQUIRE_THAT(recipe.manifests[0].selections[1], Equals("key2"));
            }
        }
    }
}
