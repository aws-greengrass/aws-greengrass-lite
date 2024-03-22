#include "deployment/deployment_manager.hpp"
#include "deployment/recipe_loader.hpp"
#include "test_tools.hpp"
#include <catch2/catch_all.hpp>

using Catch::Matchers::Equals;

// NOLINTBEGIN

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
                deployment::PlatformManifest linuxManifest = recipe.manifests[0];
                deployment::PlatformManifest darwinManifest = recipe.manifests[1];
                deployment::PlatformManifest windowsManifest = recipe.manifests[2];

                AND_THEN("The manifests contain the right platform keys") {
                    REQUIRE(linuxManifest.platform.os == "linux");
                    REQUIRE(darwinManifest.platform.os == "darwin");
                    REQUIRE(windowsManifest.platform.os == "windows");
                }
                AND_WHEN("Linux lifecycle is parsed") {
                    deployment::LifecycleSection linuxLifecycle;
                    REQUIRE_FALSE(linuxManifest.lifecycle->empty());
                    data::archive::readFromStruct(linuxManifest.lifecycle, linuxLifecycle);
                    THEN("The lifecycle section was parsed correctly") {
                        REQUIRE(linuxLifecycle.run.has_value());
                        REQUIRE_FALSE(linuxLifecycle.run->script.empty());
                        REQUIRE(
                            linuxLifecycle.run->script
                            == "python3 -u {artifacts:path}/hello_world.py "
                               "\"{configuration:/Message}\"\n");
                    }
                }

                AND_WHEN("Darwin lifecycle is parsed") {
                    deployment::LifecycleSection darwinLifecycle;
                    REQUIRE_FALSE(darwinManifest.lifecycle->empty());
                    data::archive::readFromStruct(darwinManifest.lifecycle, darwinLifecycle);
                    THEN("The lifecycle section was parsed correctly") {
                        REQUIRE(darwinLifecycle.run.has_value());
                        REQUIRE_FALSE(darwinLifecycle.run->script.empty());
                        REQUIRE(
                            darwinLifecycle.run->script
                            == "python3 -u {artifacts:path}/hello_world.py "
                               "\"{configuration:/Message}\"\n");
                    }
                }
                AND_WHEN("Windows lifecycle is parsed") {
                    deployment::LifecycleSection windowsLifecycle;
                    REQUIRE_FALSE(windowsManifest.lifecycle->empty());
                    data::archive::readFromStruct(windowsManifest.lifecycle, windowsLifecycle);
                    THEN("The lifecycle section was parsed correctly") {
                        REQUIRE(windowsLifecycle.run.has_value());
                        REQUIRE_FALSE(windowsLifecycle.run->script.empty());
                        REQUIRE(
                            windowsLifecycle.run->script
                            == "py -3 -u {artifacts:path}/hello_world.py "
                               "\"{configuration:/Message}\"\n");
                    }
                }
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
                REQUIRE(recipe.manifests[1].platform.os == "windows");
                REQUIRE(recipe.manifests[1].platform.architecture == "amd64");

                AND_WHEN("Linux lifecycle is parsed") {
                    deployment::LifecycleSection linuxLifecycle;
                    REQUIRE_FALSE(recipe.manifests[0].lifecycle->empty());
                    data::archive::readFromStruct(recipe.manifests[0].lifecycle, linuxLifecycle);
                    THEN("The lifecycle section was parsed correctly") {
                        REQUIRE(linuxLifecycle.install.has_value());
                        REQUIRE(linuxLifecycle.run.has_value());
                        REQUIRE_THAT(linuxLifecycle.install->script, Equals("echo Hello"));
                        REQUIRE_THAT(
                            linuxLifecycle.run->script,
                            Equals("apt-get update\napt-get install python3.7\n"));
                    }
                }

                REQUIRE(recipe.componentDependencies.size() == 2);
                REQUIRE(
                    recipe.componentDependencies.find("aws.greengrass.TokenExchangeService")
                    != recipe.componentDependencies.end());
                REQUIRE(
                    recipe.componentDependencies.find("aws.greengrass.S3Service")
                    != recipe.componentDependencies.end());
                REQUIRE_THAT(
                    recipe.componentDependencies.at("aws.greengrass.TokenExchangeService")
                        .versionRequirement,
                    Equals("^2.0.0"));
                REQUIRE_THAT(
                    recipe.componentDependencies.at("aws.greengrass.TokenExchangeService")
                        .dependencyType,
                    Equals("HARD"));
                REQUIRE_THAT(
                    recipe.componentDependencies.at("aws.greengrass.S3Service").versionRequirement,
                    Equals("^3.0.0"));
                REQUIRE_THAT(
                    recipe.componentDependencies.at("aws.greengrass.S3Service").dependencyType,
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
                REQUIRE(recipe.manifests[0].selections.size() == 2);
                REQUIRE_THAT(recipe.manifests[0].selections[0], Equals("key1"));
                REQUIRE_THAT(recipe.manifests[0].selections[1], Equals("key2"));
            }
        }
    }
}

// NOLINTEND
