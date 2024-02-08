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

        // Component Type
        if(recipeStruct.hasKey("componenttype")) {
            recipe.componentType =
                ComponentTypeMap.lookup(recipeStruct.get<std::string>("componenttype"))
                    .value_or(ComponentType::GENERIC);
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
        if(recipeStruct.hasKey("lifecycle")) {
            auto lifecycle = recipeStruct.get<ggapi::Struct>("lifecycle");
            loadGlobalLifecycle(lifecycle, recipe);
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
            if(depStruct.hasKey("dependencytype")) {
                dependencyProps.dependencyType =
                    DependencyTypeMap.lookup(depStruct.get<std::string>("dependencytype"))
                        .value_or(DependencyType::HARD);
            }
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
            if(platform.hasKey("nucleus")) {
                manifest.platform.nucleusType =
                    NucleusMap.lookup(platform.getValue<std::string>({"nucleus"}))
                        .value_or(NucleusType::UNKNOWN);
            }
            if(platform.hasKey("architecture")) {
                manifest.platform.architecture =
                    ArchitectureMap.lookup(platform.getValue<std::string>({"architecture"}))
                        .value_or(Architecture::UNKNOWN);
            }
        }

        // lifecycle
        if(data.hasKey("lifecycle")) {
            auto lifecycle = data.getValue<ggapi::Struct>({"lifecycle"});
            manifest.lifecycle = loadLifecycle(lifecycle);
        }

        // selections
        if(data.hasKey("selections")) {
            auto selections = data.getValue<ggapi::List>({"selections"});
            for(auto i = 0; i < selections.size(); i++) {
                auto selection = selections.get<std::string>(i);
                manifest.selections.emplace_back(std::move(selection));
            }
        }

        // artifacts
        if(data.hasKey("artifacts")) {
            auto artifacts = data.get<ggapi::List>("artifacts");
            for(auto i = 0; i < artifacts.size(); i++) {
                auto artifact = artifacts.get<ggapi::Struct>(i);
                loadArtifact(artifact, manifest);
            }
        }

        recipe.manifests.emplace_back(manifest);
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
        manifest.artifacts.emplace_back(std::move(artifact));
    }

    void RecipeLoader::loadGlobalLifecycle(const ggapi::Struct &data, deployment::Recipe &recipe) {
        // TODO: Bottom-level selection keys
        auto selections = data.keys();
        for(auto i = 0; i < selections.size(); i++) {
            auto selection = selections.get<std::string>(i);
            recipe.lifecycle.insert({selection, loadLifecycle(data)});
        }
    }

    Lifecycle RecipeLoader::loadLifecycle(const ggapi::Struct &data) {
        Lifecycle lifecycle;
        if(data.hasKey("setenv")) {
            auto envs = data.get<ggapi::Struct>("setenv");
            auto vars = envs.keys();
            for(auto idx = 0; idx < vars.size(); idx++) {
                auto varName = vars.get<std::string>(idx);
                lifecycle.environment.insert({varName, envs.get<std::string>(varName)});
            }
        }

        for(const auto &stepName : {"install", "run", "startup", "shutdown", "recover"}) {
            if(data.hasKey(stepName)) {
                lifecycle.steps.emplace_front(stepName, loadCommand(stepName, data));
            }
        }
        // TODO: Bootstrap lifecycle
        return lifecycle;
    }

    Command RecipeLoader::loadCommand(std::string_view stepName, const ggapi::Struct &data) {
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
                    command.environment.insert({varName, envs.get<std::string>(varName)});
                }
            }
        } else {
            command.script = data.getValue<std::string>({stepName});
        }

        return command;
    }
} // namespace deployment
