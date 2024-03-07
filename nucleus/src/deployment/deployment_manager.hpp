#pragma once
#include "data/shared_queue.hpp"
#include "deployment_model.hpp"
#include "plugin.hpp"
#include "recipe_loader.hpp"
#include "scope/context.hpp"
#include <thread>

#include <condition_variable>

namespace data {
    template<class K, class V>
    class SharedQueue;
}

namespace lifecycle {
    class Kernel;
}

namespace deployment {
    template<class K, class V>
    using DeploymentQueue = std::shared_ptr<data::SharedQueue<K, V>>;

    using namespace std::chrono_literals;

    class DeploymentException : public errors::Error {
    public:
        explicit DeploymentException(const std::string &msg) noexcept
            : errors::Error("DeploymentException", msg) {
        }
    };

    struct ScriptSection : public data::Serializable {
        std::optional<std::unordered_map<std::string, std::string>> envMap;
        std::string script;
        std::optional<bool> requiresPrivilege;
        std::optional<std::string> skipIf;
        std::optional<int64_t> timeout;

        void visit(data::Archive &archive) override {
            archive.setIgnoreCase();
            archive("SetEnv", envMap);
            archive("Script", script);
            archive("RequiresPrivilege", requiresPrivilege);
            archive("SkipIf", skipIf);
            archive("Timeout", timeout);
        }
    };

    struct BootstrapSection : public data::Serializable {
        std::optional<std::unordered_map<std::string, std::string>> envMap;
        std::optional<bool> bootstrapOnRollback;
        std::optional<std::string> script;
        std::optional<bool> requiresPrivilege;
        std::optional<int64_t> timeout;

        void visit(data::Archive &archive) override {
            archive.setIgnoreCase();
            archive("SetEnv", envMap);
            archive("BootstrapOnRollback", bootstrapOnRollback);
            archive("Script", script);
            archive("RequiresPrivilege", requiresPrivilege);
            archive("Timeout", timeout);
        }
    };

    struct LifecycleSection : public data::Serializable {
        std::optional<std::unordered_map<std::string, std::string>> envMap;
        std::optional<ScriptSection> install;
        std::optional<ScriptSection> run;
        std::optional<ScriptSection> startup;
        std::optional<ScriptSection> shutdown;
        std::optional<ScriptSection> recover;
        std::optional<BootstrapSection> bootstrap;
        std::optional<bool> bootstrapOnRollback;

        void helper(
            data::Archive &archive, std::string_view name, std::optional<ScriptSection> &section) {

            // Complexity is to handle behavior when a string is used instead of struct

            if(archive.isArchiving()) {
                archive(name, section);
                return;
            }
            auto sec = archive[name];
            if(!sec) {
                return;
            }
            if(!sec.keys().empty()) {
                sec(section); // map/structure
            }
            // if not a map, expected to be a script
            section.emplace();
            sec(section.value().script);
        }

        void helper(
            data::Archive &archive,
            std::string_view name,
            std::optional<BootstrapSection> &section) {

            // Complexity is to handle behavior when a string is used instead of struct

            if(archive.isArchiving()) {
                archive(name, section);
                return;
            }
            auto sec = archive[name];
            if(!sec) {
                return;
            }
            if(!sec.keys().empty()) {
                sec(section); // map/structure
            }
            // if not a map, expected to be a script
            section.emplace();
            sec(section.value().script);
        }

        void visit(data::Archive &archive) override {
            archive.setIgnoreCase();
            archive("SetEnv", envMap);
            helper(archive, "install", install);
            helper(archive, "run", run);
            helper(archive, "startup", startup);
            helper(archive, "shutdown", shutdown);
            helper(archive, "recover", recover);
            helper(archive, "bootstrap", bootstrap);
        }
    };

    class DeploymentManager : private scope::UsesContext {
        DeploymentQueue<std::string, Deployment> _deploymentQueue;
        DeploymentQueue<std::string, Recipe> _componentStore;
        static constexpr std::string_view DEPLOYMENT_ID_LOG_KEY = "DeploymentId";
        static constexpr std::string_view DISCARDED_DEPLOYMENT_ID_LOG_KEY = "DiscardedDeploymentId";
        static constexpr std::string_view GG_DEPLOYMENT_ID_LOG_KEY_NAME = "GreengrassDeploymentId";
        static constexpr auto POLLING_FREQUENCY = 2s;
        std::mutex _mutex;
        std::thread _thread;
        std::condition_variable _wake;
        std::atomic_bool _terminate{false};

        lifecycle::Kernel &_kernel;
        RecipeLoader _recipeLoader;

    public:
        explicit DeploymentManager(const scope::UsingContext &, lifecycle::Kernel &kernel);
        void start();
        void listen();
        void stop();
        void clearQueue();
        void createNewDeployment(const Deployment &);
        void cancelDeployment(const std::string &);
        void resolveDependencies(DeploymentDocument);
        void loadRecipesAndArtifacts(const Deployment &);
        void copyAndLoadRecipes(const std::filesystem::path &);
        Recipe loadRecipeFile(const std::filesystem::path &);
        void saveRecipeFile(const Recipe &);
        void copyArtifacts(std::string_view);
        void runDeploymentTask();
        ggapi::Struct createDeploymentHandler(ggapi::Task, ggapi::Symbol, ggapi::Struct);
        ggapi::Struct cancelDeploymentHandler(ggapi::Task, ggapi::Symbol, ggapi::Struct);
        static bool checkValidReplacement(const Deployment &, const Deployment &) noexcept;
    };
} // namespace deployment
