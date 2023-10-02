#pragma once
#include "data/globals.h"
#include "deployment/deployment_model.h"
#include "util/nucleus_paths.h"
#include "config/watcher.h"
#include "config/transaction_log.h"
#include <optional>
#include <filesystem>

namespace lifecycle {
    class CommandLine;
    class Kernel;

    class RootPathWatcher : public config::Watcher {
        Kernel & _kernel;
    public:
        explicit RootPathWatcher(Kernel & kernel) : _kernel(kernel) {}
        void initialized(const std::shared_ptr<config::Topics> &topics, data::StringOrd key, config::WhatHappened changeType) override;
        void changed(const std::shared_ptr<config::Topics> &topics, data::StringOrd key, config::WhatHappened changeType) override;
    };

    class Kernel : public util::RefObject<Kernel> {
        data::Global & _global;
        std::shared_ptr<util::NucleusPaths> _nucleusPaths;
        std::shared_ptr<RootPathWatcher> _rootPathWatcher;
        deployment::DeploymentStage _deploymentStageAtLaunch {deployment::DeploymentStage::DEFAULT};

    public:
        explicit Kernel(data::Global & global);

        static constexpr auto SERVICE_TYPE_TOPIC_KEY {"componentType"};
        static constexpr auto SERVICE_TYPE_TO_CLASS_MAP_KEY {"componentTypeToClassMap"};
        static constexpr auto PLUGIN_SERVICE_TYPE_NAME {"plugin"};
        static constexpr auto DEFAULT_CONFIG_YAML_FILE_READ {"config.yaml"};
        static constexpr auto DEFAULT_CONFIG_YAML_FILE_WRITE {"effectiveConfig.yaml"};
        static constexpr auto DEFAULT_CONFIG_TLOG_FILE {"config.tlog"};
        static constexpr auto DEFAULT_BOOTSTRAP_CONFIG_TLOG_FILE {"bootstrap.tlog"};
        static constexpr auto SERVICE_DIGEST_TOPIC_KEY {"service-digest"};
        static constexpr auto DEPLOYMENT_STAGE_LOG_KEY {"stage"};

        void preLaunch(CommandLine & commandLine);
        void launch();
        void overrideConfigLocation(CommandLine & commandLine, const std::filesystem::path & configFile);
        void initConfigAndTlog(CommandLine & commandLine);
        void updateDeviceConfiguration();
        void initializeNucleusFromRecipe();
        void setupProxy();
        void launchBootstrap();
        void launchLifecycle();
        void launchKernelDeployment();
        bool handleIncompleteTlogTruncation(const std::filesystem::path & tlogFile);
        void readConfigFromBackUpTLog(const std::filesystem::path & tlogFile, const std::filesystem::path & bootstrapTlogFile);
        void writeEffectiveConfigAsTransactionLog(const std::filesystem::path & tlogFile);
        void writeEffectiveConfig();

        std::shared_ptr<util::NucleusPaths> getPaths() {
            return _nucleusPaths;
        }
        config::Manager & getConfig() {
            return _global.environment.configManager;
        }
    };
}
