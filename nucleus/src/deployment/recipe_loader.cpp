#include "recipe_loader.hpp"
#include <fstream>

namespace deployment {

    Recipe RecipeLoader::read(const std::filesystem::path &file) {
        std::ifstream stream{file};
        stream.exceptions(std::ios::failbit | std::ios::badbit);
        if(!stream.is_open()) {
            throw std::runtime_error("Unable to read config file");
        }

        ggapi::Struct recipeStruct;

        std::string ext = util::lower(file.extension().generic_string());
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

        // Metadata
        loadMetadata(recipeStruct, recipe);

        // Dependencies
        if(recipeStruct.hasKey("componentdependencies")) {
            loadDependencies(recipeStruct.get<ggapi::Struct>("componentdependencies"), recipe);
        }

        // Configuration
        if(recipeStruct.hasKey("componentconfiguration")) {
            loadConfiguration(recipeStruct.get<ggapi::Struct>("componentconfiguration"), recipe);
        }

        // Manifests
        auto manifests = recipeStruct.getValue<ggapi::List>({"manifests"});
        for(auto i = 0; i < manifests.size(); i++) {
            auto platformManifest = manifests.get<ggapi::Struct>(i);
            loadManifests(platformManifest, recipe);
        }

        // Global Lifecycle
        if(recipeStruct.hasKey("lifecycle") && recipe.manifests.empty()) {
        }
        return recipe;
    }

    void RecipeLoader::loadMetadata(const ggapi::Struct &data, Recipe &recipe) {
        recipe.formatVersion = data.get<std::string>("recipeformatversion");
        recipe.componentName = data.get<std::string>("componentname");
        recipe.componentVersion = data.get<std::string>("componentversion");
        recipe.componentDescription = data.get<std::string>("componentdescription");
        if(data.hasKey("componentpublisher")) {
            recipe.componentPublisher = data.get<std::string>("componentpublisher");
        }
    }

    void RecipeLoader::loadConfiguration(const ggapi::Struct &data, Recipe &recipe) {
        recipe.configuration.defaultConfiguration =
            data.getValue<ggapi::Struct>({"defaultconfiguration"});
    }

    void RecipeLoader::loadDependencies(const ggapi::Struct &data, Recipe &recipe) {
        for(int i = 0; i < data.keys().size(); i++) {
            const auto key = data.keys().get<std::string>(i);
            const auto depStruct = data.getValue<ggapi::Struct>({key});
            DependencyProperties dependencyProps;
            dependencyProps.versionRequirement = depStruct.get<std::string>("versionrequirement");
            dependencyProps.dependencyType =
                DependencyTypeMap.lookup(depStruct.get<std::string>("dependencytype"))
                    .value_or(DependencyType::HARD);
            recipe.componentDependencies.insert({key, dependencyProps});
        }
    }

    void RecipeLoader::loadManifests(const ggapi::Struct &data, Recipe &recipe) {
        PlatformManifest manifest;

        // name
        if(data.hasKey("name")) {
            manifest.name = data.get<std::string>("name");
        }

        // os info
        if(data.hasKey("platform")) {
            auto platform = data.get<ggapi::Struct>("platform");
            if(platform.hasKey("os")) {
                manifest.platform.os =
                    OSMap.lookup(platform.getValue<std::string>({"os"})).value_or(OS::UNKNOWN);
            }
            if(platform.hasKey("architecture")) {
                manifest.platform.architecture =
                    ArchitectureMap.lookup(platform.getValue<std::string>({"architecture"}))
                        .value_or(Architecture::UNKNOWN);
            }
        }

        // lifecycle
        auto lifecycle = data.getValue<ggapi::Struct>({"lifecycle"});
        loadLifecycle(lifecycle, manifest);

        // selections
        // TODO: selection list

        // artifacts
        if(data.hasKey("artifacts")) {
            auto artifacts = data.get<ggapi::List>("artifacts");
            for(auto i = 0; i < artifacts.size(); i++) {
                auto artifact = artifacts.get<ggapi::Struct>(i);
                loadArtifact(artifact, manifest);
            }
        }

        recipe.manifests.push_back(manifest);
    }

    void RecipeLoader::loadArtifact(const ggapi::Struct &data, PlatformManifest &manifest) {
        ComponentArtifact artifact;
        artifact.uri = data.get<std::string>("uri");
        if(data.hasKey("digest")) {
            artifact.digest = data.get<std::string>("digest");
        }
        if(data.hasKey("algorithm")) {
            artifact.algorithm = data.get<std::string>("algorithm");
        }
        if(data.hasKey("unarchive")) {
            artifact.unarchive =
                UnarchiveMap.lookup(data.get<std::string>("unarchive")).value_or(Unarchive::NONE);
        }
        if(data.hasKey("permission")) {
            if(data.get<ggapi::Struct>("permission").hasKey("read")) {
                artifact.permission.read =
                    PermissionMap.lookup(data.getValue<std::string>({"permission", "read"}))
                        .value_or(PermissionType::OWNER);
            }
            if(data.get<ggapi::Struct>("permission").hasKey("execute")) {
                artifact.permission.execute =
                    PermissionMap.lookup(data.getValue<std::string>({"permission", "execute"}))
                        .value_or(PermissionType::NONE);
            }
        }
        manifest.artifacts.push_back(artifact);
    }

    void RecipeLoader::loadLifecycle(const ggapi::Struct &data, PlatformManifest &manifest) {
        if(data.hasKey("setenv")) {
            auto envs = data.get<ggapi::Struct>("setenv");
            auto vars = envs.keys();
            for(auto idx = 0; idx < vars.size(); idx++) {
                auto varName = vars.get<std::string>(idx);
                manifest.envs.insert({varName, envs.get<std::string>(varName)});
            }
        }
        if(data.hasKey("install")) {
            loadCommand("install", data, manifest);
        }
        if(data.hasKey("run")) {
            loadCommand("run", data, manifest);
        }
        if(data.hasKey("startup")) {
            loadCommand("startup", data, manifest);
        }
        if(data.hasKey("shutdown")) {
            loadCommand("shutdown", data, manifest);
        }
        if(data.hasKey("recover")) {
            loadCommand("recover", data, manifest);
        }
        // TODO: Bootstrap lifecycle
    }
    void RecipeLoader::loadCommand(
        std::string_view stepName, const ggapi::Struct &data, PlatformManifest &manifest) {
        Command command;
        if(data.isStruct(stepName)) {
            command.script = data.getValue<std::string>({"script"});
            if(data.get<ggapi::Struct>(stepName).hasKey("requiresprivilege")) {
                command.requiresPrivilege = data.getValue<bool>({"requiresprivilege"});
            }
            if(data.get<ggapi::Struct>(stepName).hasKey("skipif")) {
                command.skipIf = data.getValue<std::string>({"skipif"});
            }
            if(data.get<ggapi::Struct>(stepName).hasKey("timeout")) {
                command.timeout = data.getValue<int>({"timeout"});
            }
            if(data.get<ggapi::Struct>(stepName).hasKey("setenv")) {
                auto envs = data.getValue<ggapi::Struct>({"setenv"});
                auto vars = envs.keys();
                for(auto idx = 0; idx < vars.size(); idx++) {
                    auto varName = vars.get<std::string>(idx);
                    command.envs.insert({varName, envs.get<std::string>(varName)});
                }
            }
        } else {
            command.script = data.getValue<std::string>({"run"});
        }

        manifest.lifecycle.emplace_front(stepName, command);
    }
} // namespace deployment
