#pragma once
#include "data/shared_queue.hpp"
#include "deployment_model.hpp"
#include "package_manager/package_manager.hpp"
#include "plugin.hpp"
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

    class DeploymentManager : private scope::UsesContext {
        DeploymentQueue<std::string, Deployment> _deploymentQueue;
        //DeploymentQueue<std::string, Recipe> _componentStore;
        //std::shared_ptr<data::SharedStruct> _recipeAsStruct ;
        static constexpr std::string_view DEPLOYMENT_ID_LOG_KEY = "DeploymentId";
        static constexpr std::string_view DISCARDED_DEPLOYMENT_ID_LOG_KEY = "DiscardedDeploymentId";
        static constexpr std::string_view GG_DEPLOYMENT_ID_LOG_KEY_NAME = "GreengrassDeploymentId";
        static constexpr auto POLLING_FREQUENCY = 2s;
        std::mutex _mutex;
        std::thread _thread;
        std::condition_variable _wake;
        std::atomic_bool _terminate{false};
        ggapi::Subscription _createSubs;
        ggapi::Subscription _cancelSubs;
        ggapi::ModuleScope _module;
        package_manager::PackageManager _packageManager;
        lifecycle::Kernel &_kernel;

    public:
        explicit DeploymentManager(
            const scope::UsingContext &,
            lifecycle::Kernel &kernel,
            package_manager::PackageManager packageManager);
        void ManageConfigDeployment(std::filesystem::path deploymentPath);
        void start();
        void listen(const ggapi::ModuleScope &module);
        void stop();
        void clearQueue();
        void createNewDeployment(const Deployment &);
        void cancelDeployment(const std::string &);
        void runDeploymentTask();
        ggapi::ObjHandle createDeploymentHandler(ggapi::Symbol, const ggapi::Container &);
        ggapi::ObjHandle cancelDeploymentHandler(ggapi::Symbol, const ggapi::Container &);
        static bool checkValidReplacement(const Deployment &, const Deployment &) noexcept;
    };
} // namespace deployment
