#pragma once
#include "deployment_model.hpp"

#include "data/shared_queue.hpp"
#include "lifecycle/kernel.hpp"
#include "plugin.hpp"
#include "scope/context_full.hpp"
#include <condition_variable>
#include <yaml-cpp/yaml.h>

namespace data {
    template<class K, class V>
    class SharedQueue;
}

namespace lifecycle {
    class Kernel;
}

namespace deployment {

    using ValueType = std::variant<
        // types in same order as type consts in ValueTypes below
        bool, // BOOL
        uint64_t, // INT
        double, // DOUBLE
        std::string, // STRING
        ggapi::Struct>;

    class YamlReader {
    public:
        YamlReader() = default;
        ggapi::Struct read(const std::filesystem::path &path) {
            std::ifstream stream{path};
            stream.exceptions(std::ios::failbit | std::ios::badbit);
            if(!stream.is_open()) {
                throw std::runtime_error("Unable to read config file");
            }
            return read(stream);
        }
        ggapi::Struct read(std::istream &stream) {
            //
            // yaml-cpp has a number of flaws, but short of rewriting a YAML parser, is
            // sufficient to get going
            //
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
        ggapi::Struct rawSequenceValue(YAML::Node &node) {
            auto child = ggapi::Struct::create();
            int idx = 0;
            for(auto i : node) {
                inplaceTopicValue(child, std::to_string(idx++), rawValue(i));
            }
            return child;
        }

        // NOLINTNEXTLINE(*-no-recursion)
        ggapi::Struct rawMapValue(YAML::Node &node) {
            auto data = ggapi::Struct::create();
            for(auto i : node) {
                auto key = i.first.as<std::string>();
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
                auto key = i.first.as<std::string>();
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

    template<class K, class V>
    using DeploymentQueue = std::shared_ptr<data::SharedQueue<K, V>>;

    class DeploymentException : public errors::Error {
    public:
        explicit DeploymentException(const std::string &msg) noexcept
            : errors::Error("DeploymentException", msg) {
        }
    };

    class DeploymentManager : private scope::UsesContext {
        DeploymentQueue<std::string, Deployment> _deploymentQueue;
        DeploymentQueue<std::string, Recipe> _componentStore;
        static constexpr std::string_view DEPLOYMENT_ID_LOG_KEY = "DeploymentId";
        static constexpr std::string_view DISCARDED_DEPLOYMENT_ID_LOG_KEY = "DiscardedDeploymentId";
        static constexpr std::string_view GG_DEPLOYMENT_ID_LOG_KEY_NAME = "GreengrassDeploymentId";
        inline static const ggapi::Symbol EXECUTE_PROCESS_TOPIC{
            "aws.greengrass.Native.StartProcess"};
        mutable std::mutex _mutex;
        std::thread _thread;
        std::condition_variable _wake;
        std::atomic_bool _terminate{false};

        lifecycle::Kernel &_kernel;

    public:
        explicit DeploymentManager(const scope::UsingContext &, lifecycle::Kernel &kernel);
        void start();
        void listen();
        void stop();
        void clearQueue();
        void createNewDeployment(const Deployment &);
        void cancelDeployment(const std::string &);
        void loadRecipesAndArtifacts(const Deployment &);
        void copyAndLoadRecipes(std::string_view);
        static Recipe loadRecipeFile(const std::filesystem::path &);
        void saveRecipeFile(const Recipe &);
        void copyArtifactsAndRun(std::string_view);
        ggapi::Struct createDeploymentHandler(ggapi::Task, ggapi::Symbol, ggapi::Struct);
        ggapi::Struct cancelDeploymentHandler(ggapi::Task, ggapi::Symbol, ggapi::Struct);
        static bool checkValidReplacement(const Deployment &, const Deployment &);
        config::Manager &getConfig() {
            return context()->configManager();
        }
    };
} // namespace deployment
