#pragma once
#include "config/transaction_log.hpp"
#include "config/watcher.hpp"
#include "deployment/deployment_manager.hpp"
#include "deployment/deployment_model.hpp"
#include "deployment/device_configuration.hpp"
#include "lifecycle/kernel_alternatives.hpp"
#include "platform_abstraction/abstract_process_manager.hpp"
#include "scope/context.hpp"
#include "tasks/expire_time.hpp"
#include "tasks/task_threads.hpp"
#include "util/nucleus_paths.hpp"
#include <filesystem>
#include <optional>

namespace pubsub {
    class Promise;
}

namespace deployment {
    class DeviceConfiguration;
    class DeploymentManager;
} // namespace deployment

namespace lifecycle {
    class CommandLine;
    class Kernel;
    class KernelAlternatives;

    class RootPathWatcher : public config::Watcher {
        Kernel &_kernel;

    public:
        explicit RootPathWatcher(Kernel &kernel) : _kernel(kernel) {
        }

        void initialized(
            const std::shared_ptr<config::Topics> &topics,
            data::Symbol key,
            config::WhatHappened changeType) override;
        void changed(
            const std::shared_ptr<config::Topics> &topics,
            data::Symbol key,
            config::WhatHappened changeType) override;
    };

    class Kernel : private scope::UsesContext {
        std::shared_ptr<util::NucleusPaths> _nucleusPaths;
        std::shared_ptr<RootPathWatcher> _rootPathWatcher;
        std::shared_ptr<pubsub::Promise> _mainPromise;
        std::unique_ptr<config::TlogWriter> _tlog;
        deployment::DeploymentStage _deploymentStageAtLaunch{deployment::DeploymentStage::DEFAULT};
        std::shared_ptr<deployment::DeviceConfiguration> _deviceConfiguration{nullptr};
        std::unique_ptr<KernelAlternatives> _kernelAlts{nullptr};
        std::unique_ptr<deployment::DeploymentManager> _deploymentManager{nullptr};
        std::unique_ptr<ipc::ProcessManager> _processManager{};

    public:
        explicit Kernel(const scope::UsingContext &context);
        Kernel(const Kernel &) = delete;
        Kernel(Kernel &&) = delete;
        Kernel &operator=(const Kernel &) = delete;
        Kernel &operator=(Kernel &&) = delete;
        ~Kernel() = default;

        static constexpr auto SERVICE_TYPE_TOPIC_KEY{"componentType"};
        static constexpr auto SERVICE_TYPE_TO_CLASS_MAP_KEY{"componentTypeToClassMap"};
        static constexpr auto PLUGIN_SERVICE_TYPE_NAME{"plugin"};
        static constexpr auto DEFAULT_CONFIG_YAML_FILE_READ{"config.yaml"};
        static constexpr auto DEFAULT_CONFIG_YAML_FILE_WRITE{"effectiveConfig.yaml"};
        static constexpr auto DEFAULT_CONFIG_TLOG_FILE{"config.tlog"};
        static constexpr auto DEFAULT_BOOTSTRAP_CONFIG_TLOG_FILE{"bootstrap.tlog"};
        static constexpr auto SERVICE_DIGEST_TOPIC_KEY{"service-digest"};
        static constexpr auto DEPLOYMENT_STAGE_LOG_KEY{"stage"};
        static constexpr auto SHUTDOWN_TIMEOUT_SECONDS{30};
        static constexpr auto SERVICE_SHUTDOWN_TIMEOUT_SECONDS{5};
        static inline const data::SymbolInit SERVICES_TOPIC_KEY{"services"};
        static inline const data::SymbolInit SERVICE_LOADER_TOPIC_KEY{
            "aws.greengrass.componentType"};

        void preLaunch(CommandLine &commandLine);
        int launch();
        static void overrideConfigLocation(
            CommandLine &commandLine, const std::filesystem::path &configFile);
        void initConfigAndTlog(CommandLine &commandLine);
        void initDeviceConfiguration(CommandLine &commandLine);
        void initializeNucleusFromRecipe();
        void initializeProcessManager(CommandLine &commandLine);
        void setupProxy();
        void launchBootstrap();
        void launchRollbackBootstrap();
        void launchLifecycle();
        void launchKernelDeployment();
        static bool handleIncompleteTlogTruncation(const std::filesystem::path &tlogFile);
        void readConfigFromBackUpTLog(
            const std::filesystem::path &tlogFile, const std::filesystem::path &bootstrapTlogFile);
        void writeEffectiveConfigAsTransactionLog(const std::filesystem::path &tlogFile);
        void writeEffectiveConfig();
        void writeEffectiveConfig(const std::filesystem::path &configFile);
        std::shared_ptr<config::Topics> findServiceTopic(const std::string_view &serviceName);

        void stopAllServices(std::chrono::seconds timeoutSeconds);
        void shutdown(std::chrono::seconds timeoutSeconds, int exitCode);
        void shutdown(
            std::chrono::seconds timeoutSeconds = std::chrono::seconds(SHUTDOWN_TIMEOUT_SECONDS));

        void softShutdown(
            std::chrono::seconds expireTime = std::chrono::seconds(SHUTDOWN_TIMEOUT_SECONDS));

        std::shared_ptr<util::NucleusPaths> getPaths() {
            return _nucleusPaths;
        }

        config::Manager &getConfig();
        std::vector<std::string> getSupportedCapabilities() const;

        ipc::ProcessId startProcess(
            std::string script,
            std::chrono::seconds timeout,
            bool requiresPrivilege,
            std::unordered_map<std::string, std::optional<std::string>> env,
            const std::string &note,
            std::optional<ipc::CompletionCallback> onComplete = {});
    };
} // namespace lifecycle
