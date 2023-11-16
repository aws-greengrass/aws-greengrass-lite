#include "kernel.hpp"
#include "command_line.hpp"
#include "config/yaml_helper.hpp"
#include "deployment/device_configuration.hpp"
#include "scope/context_full.hpp"
#include "util/commitable_file.hpp"
#include <filesystem>
#include <iostream>

namespace lifecycle {
    //
    // GG-Interop:
    // GG-Java tightly couples Kernel and KernelLifecycle, this class combines functionality from
    // both. Also some functionality from KernelCommandLine is moved here.
    //

    Kernel::Kernel(const std::shared_ptr<scope::Context> &context) : _context(context) {
        _nucleusPaths = std::make_shared<util::NucleusPaths>();
        data::SymbolInit::init(_context.lock(), {&SERVICES_TOPIC_KEY});
    }

    //
    // GG-Interop:
    // In GG-Java, there's command-line post-processing in Kernel::parseArgs()
    // That logic is moved here to decouple command line processing and post-processing.
    //
    void Kernel::preLaunch(CommandLine &commandLine) {
        getConfig().publishQueue().start();
        _rootPathWatcher = std::make_shared<RootPathWatcher>(*this);
        context()
            .configManager()
            .lookup({"system", "rootpath"})
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
        if(!overrideConfigFile.empty()) {
            overrideConfigLocation(commandLine, overrideConfigFile);
        }
        initConfigAndTlog(commandLine);
        updateDeviceConfiguration(commandLine);
        initializeNucleusFromRecipe();
        setupProxy();
    }

    //
    // When a deployment in effect, override which config is used, even if it conflicts with
    // a config specified in the command line.
    //
    void Kernel::overrideConfigLocation(
        CommandLine &commandLine, const std::filesystem::path &configFile
    ) {
        if(configFile.empty()) {
            throw std::invalid_argument("Config file expected to be specified");
        }
        if(!commandLine.getProvidedConfigPath().empty()) {
            // TODO: logging, warn user of override
            std::cerr << "Ignoring specified configuration file in favor of override\n";
        }
        commandLine.setProvidedConfigPath(configFile);
    }

    //
    // TLOG has preference over config, unless customer has explicitly chosen to override.
    // The TLOG contains more type-correct information and timestamps. When reading from
    // a config file, timestamps are lost. More so, if reading from YAML, type information is
    // mostly lost.
    //
    void Kernel::initConfigAndTlog(CommandLine &commandLine) {
        std::filesystem::path transactionLogPath =
            _nucleusPaths->configPath() / DEFAULT_CONFIG_TLOG_FILE;
        bool readFromTlog = true;

        if(!commandLine.getProvidedConfigPath().empty()) {
            // Command-line override, use config instead of tlog
            getConfig().read(commandLine.getProvidedConfigPath());
            readFromTlog = false;
        } else {
            // Note: Bootstrap config is written only if override config not used
            std::filesystem::path bootstrapTlogPath =
                _nucleusPaths->configPath() / DEFAULT_BOOTSTRAP_CONFIG_TLOG_FILE;

            // config.tlog is valid if any incomplete tlog truncation is handled correctly and the
            // tlog content is validated - torn writes also handled here
            bool transactionTlogValid =
                handleIncompleteTlogTruncation(transactionLogPath)
                && config::TlogReader::handleTlogTornWrite(_context.lock(), transactionLogPath);

            if(transactionTlogValid) {
                // if config.tlog is valid, use it
                getConfig().read(transactionLogPath);
            } else {
                // if config.tlog is not valid, try to read config from backup tlogs
                readConfigFromBackUpTLog(transactionLogPath, bootstrapTlogPath);
                readFromTlog = false;
            }

            // Alternative configurations
            std::filesystem::path externalConfig =
                _nucleusPaths->configPath() / DEFAULT_CONFIG_YAML_FILE_READ;
            bool externalConfigFromCmd = !commandLine.getProvidedInitialConfigPath().empty();
            if(externalConfigFromCmd) {
                externalConfig = commandLine.getProvidedInitialConfigPath();
            }
            bool externalConfigExists = std::filesystem::exists(externalConfig);
            // If there is no tlog, or the path was provided via commandline, read in that file
            if((externalConfigFromCmd || !transactionTlogValid) && externalConfigExists) {
                getConfig().read(externalConfig);
                readFromTlog = false;
            }

            // If no bootstrap was present, then write one out now that we've loaded our config so
            // that we can fallback to something in future
            if(!std::filesystem::exists(bootstrapTlogPath)) {
                writeEffectiveConfigAsTransactionLog(bootstrapTlogPath);
            }
        }

        // If configuration built up from another source, initialize the transaction log.
        if(!readFromTlog) {
            writeEffectiveConfigAsTransactionLog(transactionLogPath);
        }
        // After each boot create a dump of what the configuration looks like
        writeEffectiveConfig();

        // hook tlog to config so that changes over time are persisted to the tlog
        _tlog = std::make_unique<config::TlogWriter>(
            _context.lock(), getConfig().root(), transactionLogPath);
        // TODO: per KernelLifecycle.initConfigAndTlog(), configure auto truncate from config
        _tlog->flushImmediately().withAutoTruncate().append().withWatcher();
    }

    void Kernel::updateDeviceConfiguration(CommandLine &commandLine) {
        _deviceConfiguration =
            std::make_unique<deployment::DeviceConfiguration>(_context.lock(), *this);
        if(!commandLine.getAwsRegion().empty()) {
            _deviceConfiguration->setAwsRegion(commandLine.getAwsRegion());
        }
        if(!commandLine.getEnvStage().empty()) {
            _deviceConfiguration->getEnvironmentStage().withValue(commandLine.getEnvStage());
        }
        if(!commandLine.getDefaultUser().empty()) {
            // TODO: platform resolver for user
        }
    }

    void Kernel::initializeNucleusFromRecipe() {
        // _kernelAlts = std::make_unique<KernelAlternatives>(_global.environment, *this);
        // TODO: missing code
    }

    void Kernel::setupProxy() {
        // TODO: missing code
    }

    bool Kernel::handleIncompleteTlogTruncation(const std::filesystem::path &tlogFile) {
        std::filesystem::path oldTlogPath = config::TlogWriter::getOldTlogPath(tlogFile);
        // At the beginning of tlog truncation, the original config.tlog file is moved to
        // config.tlog.old If .old file exists, then the last truncation was incomplete, so we need
        // to undo its effect by moving it back to the original location.
        if(std::filesystem::exists(oldTlogPath)) {
            // Don't need to validate the content of old tlog here, since the existence of old
            // tlog itself signals that the content in config.tlog at the moment is unusable
            // TODO: Log warning
            std::cerr << "Config tlog truncation was interrupted by last nucleus shutdown and "
                         "an old version of config.tlog exists. Undoing the effect of incomplete "
                         "truncation by moving "
                      << oldTlogPath << " back to " << tlogFile << "\n";
            try {
                std::filesystem::rename(oldTlogPath, tlogFile);
            } catch(std::filesystem::filesystem_error &e) {
                // TODO: Log
                std::cerr << "An IO error occurred while moving the old tlog file. "
                             "Will attempt to load from backup configs\n";
                return false;
            }
        }
        // also delete the new file (config.tlog+) as part of undoing the effect of incomplete
        // truncation
        std::filesystem::path newTlogPath = util::CommitableFile::getNewFile(tlogFile);
        try {
            if(std::filesystem::exists(newTlogPath)) {
                std::filesystem::remove(newTlogPath);
            }
        } catch(std::filesystem::filesystem_error &e) {
            // no reason to rethrow as it does not impact this code path
            // TODO: Log - Failed to delete {}
            std::cerr << "Failed to delete " << newTlogPath << "\n";
        }
        return true;
    }

    void Kernel::readConfigFromBackUpTLog(
        const std::filesystem::path &tlogFile, const std::filesystem::path &bootstrapTlogFile
    ) {
        std::vector<std::filesystem::path> paths{
            util::CommitableFile::getBackupFile(tlogFile),
            bootstrapTlogFile,
            util::CommitableFile::getBackupFile(bootstrapTlogFile)};
        for(auto backupPath : paths) {
            if(config::TlogReader::handleTlogTornWrite(_context.lock(), backupPath)) {
                // TODO: log
                std::cerr << "Transaction log " << tlogFile
                          << " is invalid, attempting to load from " << backupPath << std::endl;
                getConfig().read(backupPath);
                return;
            }
        }
    }

    void Kernel::writeEffectiveConfigAsTransactionLog(const std::filesystem::path &tlogFile) {
        config::TlogWriter(_context.lock(), getConfig().root(), tlogFile).dump();
    }

    void Kernel::writeEffectiveConfig() {
        std::filesystem::path configPath = getPaths()->configPath();
        if(!configPath.empty()) {
            writeEffectiveConfig(configPath / DEFAULT_CONFIG_YAML_FILE_WRITE);
        }
    }

    void Kernel::writeEffectiveConfig(const std::filesystem::path &configFile) {
        util::CommitableFile commitable(configFile);
        config::YamlHelper::write(_context.lock(), commitable, getConfig().root());
    }

    int Kernel::launch() {
        if(!_mainThread) {
            _mainThread.claim(std::make_shared<tasks::FixedTimerTaskThread>(_context.lock()));
        }

        switch(_deploymentStageAtLaunch) {
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
        _mainThread.release();
        return _exitCode;
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
        context().pluginLoader().discoverPlugins(getPaths()->pluginPath());
        std::shared_ptr<data::SharedStruct> lifecycleArgs{
            std::make_shared<data::SharedStruct>(_context.lock())};
        std::shared_ptr<data::ContainerModelBase> rootStruct = getConfig().root();
        lifecycleArgs->put("config", rootStruct);

        context().pluginLoader().lifecycleBootstrap(lifecycleArgs);
        context().pluginLoader().lifecycleDiscover(lifecycleArgs);
        context().pluginLoader().lifecycleStart(lifecycleArgs);
        context().pluginLoader().lifecycleRun(lifecycleArgs);

        (void) ggapiWaitForTaskCompleted(
            ggapiGetCurrentTask(), -1); // essentially blocks until kernel signalled to terminate
        context().pluginLoader().lifecycleTerminate(lifecycleArgs);
        getConfig().publishQueue().stop();
    }

    std::shared_ptr<config::Topics> Kernel::findServiceTopic(const std::string_view &serviceName) {
        std::shared_ptr<config::ConfigNode> node =
            getConfig().root()->createInteriorChild(SERVICES_TOPIC_KEY)->getNode(serviceName);
        return std::dynamic_pointer_cast<config::Topics>(node);
    }

    void RootPathWatcher::initialized(
        const std::shared_ptr<config::Topics> &topics,
        data::Symbol key,
        config::WhatHappened changeType) {
        changed(topics, key, config::WhatHappened::never);
    }

    void RootPathWatcher::changed(
        const std::shared_ptr<config::Topics> &topics,
        data::Symbol key,
        config::WhatHappened changeType) {
        config::Topic topic = topics->getTopic(key);
        if(!topic.isNull()) {
        _kernel.getPaths()->initPaths(topic.getString());
        }
    }

    void Kernel::stopAllServices(std::chrono::seconds timeoutSeconds) {
        // TODO: missing code
    }

    void Kernel::shutdown(std::chrono::seconds timeoutSeconds, int exitCode) {
        setExitCode(exitCode);
        shutdown(timeoutSeconds);
    }

    void Kernel::shutdown(std::chrono::seconds timeoutSeconds) {
        // TODO: missing code
        softShutdown(timeoutSeconds);
        // Cancel main task causes main thread to terminate, causing clean shutdown
        _mainThread.getTask()->cancelTask();
    }

    void Kernel::softShutdown(std::chrono::seconds timeoutSeconds) {
        getConfig().publishQueue().drainQueue();
        std::cerr << "Starting soft shutdown" << std::endl;
        stopAllServices(timeoutSeconds);
        std::cerr << "Closing transaction log" << std::endl;
        _tlog->commit();
        writeEffectiveConfig();
    }
    config::Manager &Kernel::getConfig() {
        return context().configManager();
    }

} // namespace lifecycle
