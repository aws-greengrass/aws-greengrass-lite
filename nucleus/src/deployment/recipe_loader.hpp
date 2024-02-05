#pragma once
#include "deployment_model.hpp"
#include "plugin.hpp"
#include "util.hpp"
#include <yaml-cpp/yaml.h>

namespace deployment {
    using ValueType = std::variant<bool, uint64_t, double, std::string, ggapi::List, ggapi::Struct>;

    struct ComponentArtifact {
        std::string digest;
        std::string algorithm;
        std::string unarchive;
        std::string permission;
    };

    struct Command {
        bool requiresPrivilege = false;
        std::string script;
    };

    enum class DependencyType : uint32_t { HARD, SOFT };

    static constexpr std::string_view HARD{"HARD"};
    static constexpr std::string_view SOFT{"SOFT"};

    inline static const util::LookupTable<std::string_view, DependencyType, 2> DependencyTypeMap{
        HARD,
        DependencyType::HARD,
        SOFT,
        DependencyType::SOFT,
    };

    struct DependencyProperties {
        std::string versionRequirement;
        DependencyType dependencyType{};
    };

    struct ComponentConfiguration {
        ggapi::Struct defaultConfiguration;
    };

    struct Platform {
        std::string os;
        std::string architecture;
    };

    struct PlatformManifest {
        Platform platform;
        std::string name;
        std::forward_list<std::tuple<std::string, Command>> lifecycle;
        std::vector<ComponentArtifact> artifacts;
        std::vector<std::string> selections;
    };

    struct Recipe {
        std::string formatVersion;
        std::string componentName;
        std::string componentVersion;
        std::string componentType;
        std::string componentDescription;
        std::string componentPublisher;
        std::string componentSource;
        ComponentConfiguration configuration;
        std::unordered_map<std::string, DependencyProperties> componentDependencies;
        std::vector<PlatformManifest> manifests;
        std::forward_list<std::tuple<std::string, Command>> lifecycle;
    };

    class RecipeLoader {
    public:
        RecipeLoader() = default;
        static Recipe read(const std::filesystem::path &file) {
            std::ifstream stream{file};
            stream.exceptions(std::ios::failbit | std::ios::badbit);
            if(!stream.is_open()) {
                throw std::runtime_error("Unable to read config file");
            }
            std::string ext = util::lower(file.extension().generic_string());
            ggapi::Struct recipeStruct;

            if(ext == ".yaml" || ext == ".yml") {
                // TODO: Do rest of the recipe and dependency resolution
                auto buf = ggapi::Buffer::create().read(stream);
                recipeStruct = ggapi::Struct{buf.fromYaml()};
            } else if(ext == ".json") {
                auto buf = ggapi::Buffer::create();
                recipeStruct = ggapi::Struct{buf.fromJson()};
            } else {
                throw std::runtime_error("Unsupported recipe file type");
            }

            Recipe recipe;
            recipe.formatVersion = recipeStruct.get<std::string>("recipeformatversion");
            recipe.componentName = recipeStruct.get<std::string>("componentname");
            recipe.componentVersion = recipeStruct.get<std::string>("componentversion");
            recipe.componentType = "GENERIC";
            recipe.componentDescription = recipeStruct.get<std::string>("componentdescription");
            if(recipeStruct.hasKey("componentpublisher")) {
                recipe.componentPublisher = recipeStruct.get<std::string>("componentpublisher");
            }
            if(recipeStruct.hasKey("componentdependencies")) {
                auto componentDependencies =
                    recipeStruct.get<ggapi::Struct>("componentdependencies");
                for(int i = 0; i < componentDependencies.keys().size(); i++) {
                    const auto key = componentDependencies.keys().get<std::string>(i);
                    const auto depStruct = componentDependencies.getValue<ggapi::Struct>({key});
                    DependencyProperties dependencyProps;
                    dependencyProps.versionRequirement =
                        depStruct.get<std::string>("versionrequirement");
                    dependencyProps.dependencyType =
                        DependencyTypeMap.lookup(depStruct.get<std::string>("dependencytype"))
                            .value_or(DependencyType::HARD);
                    recipe.componentDependencies.insert({key, dependencyProps});
                }
            }
            if(recipeStruct.hasKey("componentconfiguration")) {
                recipe.configuration.defaultConfiguration = recipeStruct.getValue<ggapi::Struct>(
                    {"componentconfiguration", "defaultconfiguration"});
            }
            auto manifests = recipeStruct.getValue<ggapi::List>({"manifests"});
            for(auto i = 0; i < manifests.size(); i++) {
                auto platformManifest = manifests.get<ggapi::Struct>(i);
                PlatformManifest manifest;
                manifest.platform.os = platformManifest.getValue<std::string>({"Platform", "os"});
                auto lifecycle = platformManifest.getValue<ggapi::Struct>({"Lifecycle"});
                if(lifecycle.hasKey("install")) {
                    Command installCommand;
                    if(lifecycle.get<ggapi::Struct>("install").hasKey("requiresprivilege")) {
                        installCommand.requiresPrivilege =
                            lifecycle.getValue<bool>({"install", "requiresprivilege"});
                    }
                    installCommand.script = lifecycle.getValue<std::string>({"install", "script"});
                    manifest.lifecycle.emplace_front("install", installCommand);
                }
                if(lifecycle.hasKey("run")) {
                    Command runCommand;
                    if(lifecycle.isStruct("run")) {
                        if(lifecycle.get<ggapi::Struct>("run").hasKey("requiresprivilege")) {
                            runCommand.requiresPrivilege =
                                lifecycle.getValue<bool>({"run", "requiresprivilege"});
                        }
                        runCommand.script = lifecycle.getValue<std::string>({"run", "script"});
                        manifest.lifecycle.emplace_front("run", runCommand);
                    } else {
                        runCommand.script = lifecycle.getValue<std::string>({"run"});
                        manifest.lifecycle.emplace_front("run", runCommand);
                    }
                }
                recipe.manifests.push_back(manifest);
            }
            return recipe;
        }
    };
} // namespace deployment
