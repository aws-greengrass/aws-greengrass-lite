#include "kernel.h"
#include "command_line.h"
#include <filesystem>

namespace lifecycle {
    //
    // Note that in GG-Java, the command line is first parsed by
    // GreengrassSetup, and some commands are then passed to Kernel
    //
    // There are three somewhat intertwined objects - Kernel, CommandLine and Kernel
    //
    // In GG-Lite, all commandline parsing is delegated to this class
    // and values injected directly into Kernel
    // which combines Kernel and Kernel
    //

    Kernel::Kernel(data::Global & global) :
        _global{global} {
        _nucleusPaths = std::make_shared<util::NucleusPaths>();
    }

    //
    // In GG-Java, there's command-line post-processing in Kernel::parseArgs()
    // That logic is moved here
    void Kernel::preLaunch(CommandLine & commandLine) {
        _rootPathWatcher = std::make_shared<RootPathWatcher>(*this);
        _global.environment.configManager
            .lookup()["system"].get("rootpath")
            .dflt(getPaths()->rootPath().generic_string())
            .addWatcher(_rootPathWatcher, config::WhatHappened::changed);

        // TODO: determine deployment stage through KernelAlternatives
        deployment::DeploymentStage stage = deployment::DeploymentStage::DEFAULT;
        std::filesystem::path overrideConfigFile;
        switch(stage) {
            case deployment::DeploymentStage::KERNEL_ACTIVATION:
            case deployment::DeploymentStage::BOOTSTRAP:
                _deploymentStageAtLaunch = stage;
                throw std::runtime_error("TODO: preLaunch() stages");
            case deployment::DeploymentStage::KERNEL_ROLLBACK:
                _deploymentStageAtLaunch = stage;
                throw std::runtime_error("TODO: preLaunch() stages");
            default:
                break;
        }
        if (!overrideConfigFile.empty()) {
            overrideConfigLocation(commandLine, overrideConfigFile);
        }
        initConfigAndTlog(commandLine);
        updateDeviceConfiguration();
        initializeNucleusFromRecipe();
        setupProxy();
    }

    void Kernel::overrideConfigLocation(CommandLine & commandLine, const std::filesystem::path &configFile) {
        //
        // This configFile is provided due to a deployment in effect, and overrides that in commandLine
        //
        if (configFile.empty()) {
            throw std::invalid_argument("Config file expected to be specified");
        }
        if (!commandLine.getProvidedConfigPath().empty()) {
            // TODO: logging, warn user of override
        }
        commandLine.setProvidedConfigPath(configFile);
    }

    void Kernel::initConfigAndTlog(CommandLine & commandLine) {
        std::filesystem::path transactionLogPath =
                _nucleusPaths->configPath() / DEFAULT_CONFIG_TLOG_FILE;
        bool readFromTlog = true;

        if (!commandLine.getProvidedConfigPath().empty()) {
            getConfig().read(commandLine.getProvidedConfigPath());
            readFromTlog = false;
        } else {
            std::filesystem::path bootstrapTlogPath =
                    _nucleusPaths->configPath() / DEFAULT_BOOTSTRAP_CONFIG_TLOG_FILE;

            // config.tlog is valid if any incomplete tlog truncation is handled correctly and the tlog content
            // is validated
            bool transactionTlogValid =
                    handleIncompleteTlogTruncation(transactionLogPath) &&
                        config::TlogReader::validateTlog(transactionLogPath);

            // if config.tlog is valid, read the tlog first because the yaml config file may not be up to date
            if (transactionTlogValid) {
                getConfig().read(transactionLogPath);
            } else {
                // if config.tlog is not valid, try to read config from backup tlogs
                readConfigFromBackUpTLog(transactionLogPath, bootstrapTlogPath);
                readFromTlog = false;
            }

            // read from external configs
            std::filesystem::path externalConfig = _nucleusPaths->configPath() / DEFAULT_CONFIG_YAML_FILE_READ;
            bool externalConfigFromCmd = !commandLine.getProvidedInitialConfigPath().empty();
            if (externalConfigFromCmd) {
                externalConfig = commandLine.getProvidedInitialConfigPath();
            }
            // not validating its content since the file could be in non-tlog format
            bool externalConfigExists = std::filesystem::exists(externalConfig);
            // If there is no tlog, or the path was provided via commandline, read in that file
            if ((externalConfigFromCmd || !transactionTlogValid) && externalConfigExists) {
                getConfig().read(externalConfig);
                readFromTlog = false;
            }

            // If no bootstrap was present, then write one out now that we've loaded our config so that we can
            // fallback to something in future
            if (!std::filesystem::exists(bootstrapTlogPath)) {
                writeEffectiveConfigAsTransactionLog(bootstrapTlogPath);
            }
        }

        // write new tlog and config files
        // only dump out the current config if we read from a source which was not the tlog
        if (!readFromTlog) {
            writeEffectiveConfigAsTransactionLog(transactionLogPath);
        }
        writeEffectiveConfig();
//
//        // hook tlog to config so that changes over time are persisted to the tlog
//        tlog = config::TlogWriter::logTransactionsTo(getConfig(), transactionLogPath)
//                .flushImmediately(true).withAutoTruncate(getContext());
    }

    void Kernel::updateDeviceConfiguration() {
        // TODO: missing code
    }

    void Kernel::initializeNucleusFromRecipe() {
        // TODO: missing code
    }

    void Kernel::setupProxy() {
        // TODO: missing code
    }

    bool Kernel::handleIncompleteTlogTruncation(const std::filesystem::path & tlogFile) {
        // TODO: missing code
        return false;
    }

    void Kernel::readConfigFromBackUpTLog(const std::filesystem::path & tlogFile, const std::filesystem::path & bootstrapTlogFile) {
        // TODO: missing code

    }

    void Kernel::writeEffectiveConfigAsTransactionLog(const std::filesystem::path & tlogFile) {
        config::TlogWriter::dump(_global.environment, getConfig().root(), tlogFile);
    }

    void Kernel::writeEffectiveConfig() {
        // TODO: missing code

    }


    void Kernel::launch() {
        if (ggapiGetCurrentTask() == 0) {
            (void) ggapiClaimThread(); // ensure current thread is associated with a thread-task
        }

        switch (_deploymentStageAtLaunch) {
            case deployment::DeploymentStage::DEFAULT:
                launchLifecycle();
                break;
            case deployment::DeploymentStage::BOOTSTRAP:
                launchBootstrap();
                break;
            case deployment::DeploymentStage::KERNEL_ACTIVATION:
            case deployment::DeploymentStage::KERNEL_ROLLBACK:
                launchKernelDeployment();
                break;
            default:
                throw std::runtime_error("Provided deployment stage at launch is not understood");
        }
    }

    void Kernel::launchBootstrap() {
        throw std::runtime_error("TODO: launchBootstrap()");
    }

    void Kernel::launchKernelDeployment() {
        throw std::runtime_error("TODO: launchKernelDeployment()");
    }

    void Kernel::launchLifecycle() {
        //
        // TODO: This is stub/sample code
        //
        _global.loader->discoverPlugins(); // TODO: replace with looking in plugin directory
        std::shared_ptr<data::StructModelBase> emptyStruct {std::make_shared<data::SharedStruct>(_global.environment)}; // TODO, empty for now
        _global.loader->lifecycleBootstrap(emptyStruct);
        _global.loader->lifecycleDiscover(emptyStruct);
        _global.loader->lifecycleStart(emptyStruct);
        _global.loader->lifecycleRun(emptyStruct);

        (void)ggapiWaitForTaskCompleted(ggapiGetCurrentTask(), -1); // essentially blocks forever but allows main thread to do work
        _global.loader->lifecycleTerminate(emptyStruct);
    }

    void RootPathWatcher::initialized(const std::shared_ptr<config::Topics> &topics, data::StringOrd key,
                                  config::WhatHappened changeType) {
        changed(topics, key, config::WhatHappened::never);
    }

    void RootPathWatcher::changed(const std::shared_ptr<config::Topics> &topics, data::StringOrd key,
                                  config::WhatHappened changeType) {
        config::Topic topic = topics->getChild(key);
        if (topic.get()) {
            _kernel.getPaths()->initPaths(topic.getString());
        }
    }
}
