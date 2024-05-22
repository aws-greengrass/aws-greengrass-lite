#include "test_tools.hpp"
#include <catch2/catch_all.hpp>
#include <data/shared_buffer.hpp>
#include <deployment/deployment_manager.hpp>
#include <package_manager/recipe_loader.hpp>
#include <package_manager/recipe_model.hpp>

using Catch::Matchers::Equals;

// NOLINTBEGIN

SCENARIO("Recipe Reader", "[deployment]") {
    auto samples = test::samples();
    GIVEN("An instance of recipe reader") {
        auto yaml_reader = package_manager::RecipeLoader();
        WHEN("Reading a hello world recipe") {
            auto recipe = yaml_reader.read(samples / "hello_recipe.yml");
            auto recipeAsStruct = yaml_reader.readAsStruct(samples / "hello_recipe.yml");
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
                    auto selectedManifest =
                        recipeAsStruct->get(recipeAsStruct->foldKey("Manifests", true))
                            .castObject<data::ListModelBase>()
                            ->get(0)
                            .castObject<data::StructModelBase>();
                    REQUIRE_FALSE(selectedManifest->empty());
                    auto linuxLifecycle =
                        selectedManifest->get(selectedManifest->foldKey("Lifecycle", true))
                            .castObject<data::StructModelBase>();

                    auto val = linuxLifecycle->toJson();
                    // auto buffer = val.get<std::vector<char>>(0, size());
                    linuxLifecycle->toJson()->write(std::cout);
                    std::cout << std::endl;
                    REQUIRE_FALSE(linuxLifecycle->empty());

                    THEN("The lifecycle section was parsed correctly") {
                        auto run =
                            linuxLifecycle->get(linuxLifecycle->foldKey("run", true)).getString();
                        REQUIRE_FALSE(run.empty());
                        REQUIRE(
                            run
                            == "python3 -u {artifacts:path}/hello_world_linux.py "
                               "\"{configuration:/Message}\"\n");
                    }
                }

                AND_WHEN("Darwin lifecycle is parsed") {
                    auto selectedManifest =
                        recipeAsStruct->get(recipeAsStruct->foldKey("Manifests", true))
                            .castObject<data::ListModelBase>()
                            ->get(1)
                            .castObject<data::StructModelBase>();
                    REQUIRE_FALSE(selectedManifest->empty());
                    auto darwinLifecycle =
                        selectedManifest->get(selectedManifest->foldKey("Lifecycle", true))
                            .castObject<data::StructModelBase>();
                    REQUIRE_FALSE(darwinLifecycle->empty());

                    THEN("The lifecycle section was parsed correctly") {
                        auto run =
                            darwinLifecycle->get(darwinLifecycle->foldKey("run", true)).getString();
                        REQUIRE_FALSE(run.empty());
                        REQUIRE(
                            run
                            == "python3 -u {artifacts:path}/hello_world_darwin.py "
                               "\"{configuration:/Message}\"\n");
                    }
                }
                AND_WHEN("Windows lifecycle is parsed") {
                    auto selectedManifest =
                        recipeAsStruct->get(recipeAsStruct->foldKey("Manifests", true))
                            .castObject<data::ListModelBase>()
                            ->get(2)
                            .castObject<data::StructModelBase>();
                    REQUIRE_FALSE(selectedManifest->empty());
                    auto windowsLifecycle =
                        selectedManifest->get(selectedManifest->foldKey("Lifecycle", true))
                            .castObject<data::StructModelBase>();
                    REQUIRE_FALSE(windowsLifecycle->empty());

                    THEN("The lifecycle section was parsed correctly") {
                        auto run = windowsLifecycle->get(windowsLifecycle->foldKey("run", true))
                                       .getString();
                        REQUIRE_FALSE(run.empty());
                        REQUIRE(
                            run
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

                // AND_WHEN("Linux lifecycle is parsed") {
                //     deployment::LifecycleSection linuxLifecycle;
                //     REQUIRE_FALSE(recipe.manifests[0].lifecycle->empty());
                //     data::archive::readFromStruct(recipe.manifests[0].lifecycle, linuxLifecycle);
                //     THEN("The lifecycle section was parsed correctly") {
                //         REQUIRE(linuxLifecycle.install.has_value());
                //         REQUIRE(linuxLifecycle.run.has_value());
                //         REQUIRE_THAT(linuxLifecycle.install->script, Equals("echo Hello"));
                //         REQUIRE_THAT(
                //             linuxLifecycle.run->script,
                //             Equals("apt-get update\napt-get install python3.7\n"));
                //     }
                // }

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
                // REQUIRE(recipe.manifests[0].selections.size() == 2);
                // REQUIRE_THAT(recipe.manifests[0].selections[0], Equals("key1"));
                // REQUIRE_THAT(recipe.manifests[0].selections[1], Equals("key2"));
            }
        }
    }
}

// NOLINTEND
