#pragma once
#include "config/transaction_log.hpp"
#include "config/watcher.hpp"
#include "data/globals.hpp"
#include "deployment/deployment_model.hpp"
#include "deployment/device_configuration.hpp"
#include "lifecycle/kernel_alternatives.hpp"
#include "tasks/expire_time.hpp"
#include "tasks/task_threads.hpp"
#include "util/nucleus_paths.hpp"
#include <filesystem>
#include <optional>

namespace deployment {
    class DeviceConfiguration;
}

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
            data::StringOrd key,
            config::WhatHappened changeType
        ) override;
        void changed(
            const std::shared_ptr<config::Topics> &topics,
            data::StringOrd key,
            config::WhatHappened changeType
        ) override;
    };

    class Kernel : public util::RefObject<Kernel> {
        data::Global &_global;
        std::shared_ptr<util::NucleusPaths> _nucleusPaths;
        std::shared_ptr<RootPathWatcher> _rootPathWatcher;
        tasks::FixedTaskThreadScope _mainThread;
        std::unique_ptr<config::TlogWriter> _tlog;
        deployment::DeploymentStage _deploymentStageAtLaunch{deployment::DeploymentStage::DEFAULT};
        std::unique_ptr<deployment::DeviceConfiguration> _deviceConfiguration{nullptr};
        std::unique_ptr<KernelAlternatives> _kernelAlts{nullptr};
        std::atomic_int _exitCode{0};

    public:
        explicit Kernel(data::Global &global);

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
        const data::StringOrdInit SERVICES_TOPIC_KEY{"services"};

        void preLaunch(CommandLine &commandLine);
        int launch();
        static void overrideConfigLocation(
            CommandLine &commandLine, const std::filesystem::path &configFile
        );
        void initConfigAndTlog(CommandLine &commandLine);
        void updateDeviceConfiguration(CommandLine &commandLine);
        void initializeNucleusFromRecipe();
        void setupProxy();
        void launchBootstrap();
        void launchLifecycle();
        void launchKernelDeployment();
        static bool handleIncompleteTlogTruncation(const std::filesystem::path &tlogFile);
        void readConfigFromBackUpTLog(
            const std::filesystem::path &tlogFile, const std::filesystem::path &bootstrapTlogFile
        );
        void writeEffectiveConfigAsTransactionLog(const std::filesystem::path &tlogFile);
        void writeEffectiveConfig();
        void writeEffectiveConfig(const std::filesystem::path &configFile);
        std::shared_ptr<config::Topics> findServiceTopic(const std::string_view &serviceName);

        void stopAllServices(std::chrono::seconds timeoutSeconds);
        void shutdown(std::chrono::seconds timeoutSeconds, int exitCode);
        void shutdown(
            std::chrono::seconds timeoutSeconds = std::chrono::seconds(SHUTDOWN_TIMEOUT_SECONDS)
        );

        void softShutdown(
            std::chrono::seconds expireTime = std::chrono::seconds(SHUTDOWN_TIMEOUT_SECONDS)
        );

        std::shared_ptr<util::NucleusPaths> getPaths() {
            return _nucleusPaths;
        }

        config::Manager &getConfig() {
            return _global.environment.configManager;
        }

        void setExitCode(int exitCode) {
            _exitCode.store(exitCode);
        }
    };
} // namespace lifecycle
