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

    inline static const std::unordered_map<std::string, DependencyType> DependencyTypeMap{
        {"HARD", DependencyType::HARD},
        {"SOFT", DependencyType::SOFT},
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

    // TODO: Refactor into different namespace
    class RecipeLoader {
    public:
        RecipeLoader() = default;
        Recipe read(const std::filesystem::path &path) {
            std::ifstream stream{path};
            stream.exceptions(std::ios::failbit | std::ios::badbit);
            if(!stream.is_open()) {
                throw std::runtime_error("Unable to read config file");
            }
            auto recipeStruct = read(stream);
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
                        DependencyTypeMap.at(depStruct.get<std::string>("dependencytype"));
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
                manifest.platform.os = platformManifest.getValue<std::string>({"platform", "os"});
                auto lifecycle = platformManifest.getValue<ggapi::Struct>({"lifecycle"});
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
        ggapi::Struct read(std::istream &stream) {
            YAML::Node root = YAML::Load(stream);
            return begin(root);
        }
        ggapi::Struct begin(YAML::Node &node) {
            auto rootStruct = ggapi::Struct::create();
            inplaceMap(rootStruct, node);
            return rootStruct;
        }

        // NOLINTNEXTLINE(*-no-recursion)
        ValueType rawValue(YAML::Node &node) {
            switch(node.Type()) {
                case YAML::NodeType::Map:
                    return rawMapValue(node);
                case YAML::NodeType::Sequence:
                    return rawSequenceValue(node);
                case YAML::NodeType::Scalar:
                    return node.as<std::string>();
                default:
                    break;
            }
            return {};
        }

        // NOLINTNEXTLINE(*-no-recursion)
        ggapi::List rawSequenceValue(YAML::Node &node) {
            auto child = ggapi::List::create();
            int idx = 0;
            for(auto i : node) {
                std::visit(
                    [&idx, &child](auto &&arg) {
                        using T = std::decay_t<decltype(arg)>;
                        if constexpr(std::is_same_v<T, bool>) {
                            child.put(idx++, arg);
                        }
                        if constexpr(std::is_same_v<T, uint64_t>) {
                            child.put(idx++, arg);
                        }
                        if constexpr(std::is_same_v<T, double>) {
                            child.put(idx++, arg);
                        }
                        if constexpr(std::is_same_v<T, std::string>) {
                            child.put(idx++, arg);
                        }
                        if constexpr(std::is_same_v<T, ggapi::Struct>) {
                            child.put(idx++, arg);
                        }
                        if constexpr(std::is_same_v<T, ggapi::List>) {
                            child.put(idx++, arg);
                        }
                    },
                    rawValue(i));
            }
            return child;
        }

        // NOLINTNEXTLINE(*-no-recursion)
        ggapi::Struct rawMapValue(YAML::Node &node) {
            auto data = ggapi::Struct::create();
            for(auto i : node) {
                auto key = util::lower(i.first.as<std::string>());
                inplaceTopicValue(data, key, rawValue(i.second));
            }
            return data;
        }

        // NOLINTNEXTLINE(*-no-recursion)
        void inplaceMap(ggapi::Struct &data, YAML::Node &node) {
            if(!node.IsMap()) {
                throw std::runtime_error("Expecting a map or sequence");
            }
            for(auto i : node) {
                auto key = util::lower(i.first.as<std::string>());
                inplaceValue(data, key, i.second);
            }
        }

        // NOLINTNEXTLINE(*-no-recursion)
        void inplaceValue(ggapi::Struct &data, const std::string &key, YAML::Node &node) {
            switch(node.Type()) {
                case YAML::NodeType::Map:
                    nestedMapValue(data, key, node);
                    break;
                case YAML::NodeType::Sequence:
                case YAML::NodeType::Scalar:
                case YAML::NodeType::Null:
                    inplaceTopicValue(data, key, rawValue(node));
                    break;
                default:
                    // ignore anything else
                    break;
            }
        }

        // NOLINTNEXTLINE(*-no-recursion)
        void inplaceTopicValue(ggapi::Struct &data, const std::string &key, const ValueType &vt) {
            std::visit(
                [key, &data](auto &&arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr(std::is_same_v<T, bool>) {
                        data.put(key, arg);
                    }
                    if constexpr(std::is_same_v<T, uint64_t>) {
                        data.put(key, arg);
                    }
                    if constexpr(std::is_same_v<T, double>) {
                        data.put(key, arg);
                    }
                    if constexpr(std::is_same_v<T, std::string>) {
                        data.put(key, arg);
                    }
                    if constexpr(std::is_same_v<T, ggapi::Struct>) {
                        data.put(key, arg);
                    }
                    if constexpr(std::is_same_v<T, ggapi::List>) {
                        data.put(key, arg);
                    }
                },
                vt);
        }

        // NOLINTNEXTLINE(*-no-recursion)
        void nestedMapValue(ggapi::Struct &data, const std::string &key, YAML::Node &node) {
            auto child = ggapi::Struct::create();
            data.put(key, child);
            inplaceMap(child, node);
        }
    };
} // namespace deployment
