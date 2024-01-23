#pragma once
#include "deployment_model.hpp"

#include "data/shared_queue.hpp"
#include "plugin.hpp"
#include "scope/context_full.hpp"
#include <condition_variable>

namespace data {
    template<class K, class V>
    class SharedQueue;
}

namespace deployment {
    template<class K, class V>
    using DeploymentQueue = std::shared_ptr<data::SharedQueue<K, V>>;

    class DeploymentException : public errors::Error {
    public:
        explicit DeploymentException(const std::string &msg) noexcept
            : errors::Error("DeploymentException", msg) {
        }
    };

    class DeploymentManager : private scope::UsesContext {
        DeploymentQueue<ggapi::Symbol, ggapi::Struct> _deploymentQueue;
        static constexpr std::string_view DEPLOYMENT_ID_LOG_KEY = "DeploymentId";
        static constexpr std::string_view DISCARDED_DEPLOYMENT_ID_LOG_KEY = "DiscardedDeploymentId";
        static constexpr std::string_view GG_DEPLOYMENT_ID_LOG_KEY_NAME = "GreengrassDeploymentId";
        mutable std::mutex _mutex;
        std::thread _thread;
        std::condition_variable _wake;
        std::atomic_bool _terminate{false};

    public:
        explicit DeploymentManager(const scope::UsingContext &);
        void start();
        void listen();
        void stop();
        void clearQueue();
        void createNewDeployment(const ggapi::Struct &);
        void cancelDeployment(const std::string &);
        void loadRecipesAndArtifacts(const ggapi::Struct &);
        ggapi::Struct createDeploymentHandler(ggapi::Task, ggapi::Symbol, ggapi::Struct);
        ggapi::Struct cancelDeploymentHandler(ggapi::Task, ggapi::Symbol, ggapi::Struct);
        static bool checkValidReplacement(const ggapi::Struct &, const ggapi::Struct &);
        config::Manager &getConfig() {
            return context()->configManager();
        }
    };
} // namespace deployment
