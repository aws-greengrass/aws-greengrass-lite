#include "test_util.hpp"
#include <catch2/catch_all.hpp>
#include <temp_module.hpp>

#include <filesystem>

using Catch::Matchers::Equals;
namespace gen_component_loader_test {
    SCENARIO("Recipe Reader", "[TestGenComponentLoader]") {
        util::TempModule testModule("json-test");
        
        GIVEN("An instance of recipe structure") {
            const auto recipeAsString = R"(---
RecipeFormatVersion: "2020-01-25"
ComponentName: com.example.HelloWorld
ComponentVersion: "1.0.0"
ComponentDescription: My first AWS IoT Greengrass component.
ComponentPublisher: Amazon
ComponentConfiguration:
  DefaultConfiguration:
    Message: world
Manifests:
  - Platform:
      os: linux
    Lifecycle:
      Startup: 
        RequiresPrivilege: false
        Script: touch ./testFile.txt 
  - Platform:
      os: darwin
    Lifecycle:
      Run: |
        python3 -u {artifacts:path}/hello_worldDarwin.py "{configuration:/Message}"
  - Platform:
      os: windows
    Lifecycle:
      Run: |
        py -3 -u {artifacts:path}/hello_world.py "{configuration:/Message}"
)";

            WHEN("a hello world recipe is converted to a Struct") {
                ggapi::Buffer buffer = ggapi::Buffer::create();
                buffer.put(0, std::string_view(recipeAsString));
                ggapi::Container c = buffer.fromYaml();
                ggapi::Struct recipeAsStruct(c);

                AND_WHEN("Linux lifecycle is parsed") {
                    GenComponentDelegate::LifecycleSection linuxLifecycle;
                    recipeAsStruct.toJson().write(std::cout);
                    std::cout << std::endl;

                    auto linuxManifest =
                        recipeAsStruct.get<ggapi::List>(recipeAsStruct.foldKey("Manifests"))
                            .get<ggapi::Struct>(0);
                    auto lifecycleAsStruct =
                        linuxManifest.get<ggapi::Struct>(linuxManifest.foldKey("Lifecycle"));
                    ggapi::Archive::transform<ggapi::ContainerDearchiver>(
                        linuxLifecycle, lifecycleAsStruct);

                    THEN("The lifecycle section without script section is archived correctly") {
                        REQUIRE(linuxLifecycle.startup.has_value());
                        REQUIRE_FALSE(linuxLifecycle.startup->script.empty());
                        REQUIRE(
                            linuxLifecycle.startup->script
                            == "python3 -u {artifacts:path}/hello_worldLinux.py "
                               "\"{configuration:/Message}\"\n");
                    }
                    // TODO:: Write a good integration test
                    AND_WHEN("Recipe and manifest is published on the topic") {
                        auto data_pack = ggapi::Struct::create();
                        data_pack.put("recipe", recipeAsStruct);
                        data_pack.put("manifest", linuxManifest);
                        data_pack.put("artifactPath", "Path");

                        auto responseFuture = ggapi::Subscription::callTopicFirst(
                            ggapi::Symbol{"componentType::aws.greengrass.generic"}, data_pack);
                        REQUIRE(responseFuture);
                        THEN("Manage generic component's lifecycle") {
                            auto response = ggapi::Struct{responseFuture.waitAndGetValue()};
                            auto genLifecycle = response.get<ggapi::ModuleScope>("moduleHandle");

                            genLifecycle.onInitialize(configAsStruct);
                            genLifecycle.onStart(configAsStruct);
                        }
                    }
                }
            }
        }
    }
} // namespace gen_component_loader_test