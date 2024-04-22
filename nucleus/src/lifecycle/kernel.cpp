#include "kernel.hpp"
#include "command_line.hpp"
#include "config/yaml_config.hpp"
#include "deployment/device_configuration.hpp"
#include "deployment/recipe_model.hpp"
#include "lifecycle/lifecycle_manager.hpp"
#include "logging/log_queue.hpp"
#include "pubsub/local_topics.hpp"
#include "scope/context_full.hpp"
#include "util/commitable_file.hpp"
#include <filesystem>
#include <memory>
#include <optional>

const auto LOG = // NOLINT(cert-err58-cpp)
    logging::Logger::of("com.aws.greengrass.lifecycle.Kernel");

namespace lifecycle {
    //
    // GG-Interop:
    // GG-Java tightly couples Kernel and KernelLifecycle, this class combines functionality
    // from both. Also, some functionality from KernelCommandLine is moved here.
    //

    Kernel::Kernel(const scope::UsingContext &context)
        : scope::UsesContext(context),
          _lifecycleManager(std::make_unique<LifecycleManager>(context, *this)) {
        _nucleusPaths = std::make_shared<util::NucleusPaths>();
        _deploymentManager = std::make_unique<deployment::DeploymentManager>(context, *this);
        data::SymbolInit::init(context, {&SERVICES_TOPIC_KEY});
    }

    Kernel::~Kernel() noexcept = default;

    //
    // GG-Interop:
    // In GG-Java, there's command-line post-processing in Kernel::parseArgs()
    // That logic is moved here to decouple command line processing and post-processing.
    //
    void Kernel::preLaunch(CommandLine &commandLine) {
        getConfig().publishQueue().start();
        _rootPathWatcher = std::make_shared<RootPathWatcher>(*this);
        context()
            ->configManager()
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
        initDeviceConfiguration(commandLine);
        initializeNucleusFromRecipe();
    }

    //
    // When a deployment in effect, override which config is used, even if it conflicts with
    // a config specified in the command line.
    //
    void Kernel::overrideConfigLocation(
        CommandLine &commandLine, const std::filesystem::path &configFile) {
        if(configFile.empty()) {
            throw std::invalid_argument("Config file expected to be specified");
        }
        if(!commandLine.getProvidedConfigPath().empty()) {
            LOG.atWarn("boot")
                .kv("configFileInput", commandLine.getProvidedConfigPath().generic_string())
                .kv("configOverride", configFile.generic_string())
                .log("Detected ongoing deployment. Ignore the config file from input and use "
                     "config file override");
        }
        commandLine.setProvidedConfigPath(configFile);
    }

    //
    // TLOG has a preference over config, unless customer has explicitly chosen to override.
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
                && config::TlogReader::handleTlogTornWrite(context(), transactionLogPath);

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
            // that we can fall back to something in the future
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
        _tlog =
            std::make_unique<config::TlogWriter>(context(), getConfig().root(), transactionLogPath);
        // TODO: per KernelLifecycle.initConfigAndTlog(), configure auto truncate from config
        _tlog->flushImmediately().withAutoTruncate().append().withWatcher();
    }

    void Kernel::initDeviceConfiguration(CommandLine &commandLine) {
        _deviceConfiguration = deployment::DeviceConfiguration::create(context(), *this);
        // std::make_shared<deployment::DeviceConfiguration>();
        if(!commandLine.getAwsRegion().empty()) {
            _deviceConfiguration->setAwsRegion(commandLine.getAwsRegion());
        }
        if(!commandLine.getEnvStage().empty()) {
            _deviceConfiguration->getEnvironmentStage().withValue(commandLine.getEnvStage());
        }
        if(!commandLine.getDefaultUser().empty()) {
#if defined(_WIN32)
            _deviceConfiguration->getRunWithDefaultWindowsUser().withValue(
                commandLine.getDefaultUser());
#else
            _deviceConfiguration->getRunWithDefaultPosixUser().withValue(
                commandLine.getDefaultUser());
#endif
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
            LOG.atWarn("boot")
                .kv("configFile", tlogFile.generic_string())
                .kv("backupConfigFile", oldTlogPath.generic_string())
                .log(
                    "Config tlog truncation was interrupted by last nucleus shutdown and an old "
                    "version of config.tlog exists. Undoing the effect of incomplete truncation by "
                    "restoring backup config");
            try {
                std::filesystem::rename(oldTlogPath, tlogFile);
            } catch(std::filesystem::filesystem_error &e) {
                LOG.atWarn("boot")
                    .kv("configFile", tlogFile.generic_string())
                    .kv("backupConfigFile", oldTlogPath.generic_string())
                    .log("An IO error occurred while moving the old tlog file. Will attempt to "
                         "load from backup configs");
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
            LOG.atWarn("boot")
                .kv("configFile", newTlogPath.generic_string())
                .cause(e)
                .log("Failed to delete partial config file");
        }
        return true;
    }

    void Kernel::readConfigFromBackUpTLog(
        const std::filesystem::path &tlogFile, const std::filesystem::path &bootstrapTlogFile) {
        std::vector<std::filesystem::path> paths{
            util::CommitableFile::getBackupFile(tlogFile),
            bootstrapTlogFile,
            util::CommitableFile::getBackupFile(bootstrapTlogFile)};
        for(const auto &backupPath : paths) {
            if(config::TlogReader::handleTlogTornWrite(context(), backupPath)) {
                LOG.atWarn("boot")
                    .kv("configFile", tlogFile.generic_string())
                    .kv("backupFile", backupPath.generic_string())
                    .log("Transaction log is invalid, attempting to load from backup");
                getConfig().read(backupPath);
                return;
            }
        }
        LOG.atWarn("boot")
            .kv("configFile", tlogFile.generic_string())
            .log("Transaction log is invalid and no usable backup exists. Either an initial "
                 "Nucleus setup is ongoing or all config tlogs were corrupted");
    }

    void Kernel::writeEffectiveConfigAsTransactionLog(const std::filesystem::path &tlogFile) {
        config::TlogWriter(context(), getConfig().root(), tlogFile).dump();
    }

    void Kernel::writeEffectiveConfig() {
        std::filesystem::path configPath = getPaths()->configPath();
        if(!configPath.empty()) {
            writeEffectiveConfig(configPath / DEFAULT_CONFIG_YAML_FILE_WRITE);
        }
    }

    void Kernel::writeEffectiveConfig(const std::filesystem::path &configFile) {
        util::CommitableFile commitable(configFile);
        config::YamlConfigHelper::write(context(), commitable, getConfig().root());
    }

    int Kernel::launch() {
        data::Symbol deploymentSymbol =
            deployment::DeploymentConsts::STAGE_MAP.rlookup(_deploymentStageAtLaunch)
                .value_or(data::Symbol{});

        _deploymentManager->start();

        switch(_deploymentStageAtLaunch) {
            case deployment::DeploymentStage::DEFAULT:
                LOG.atInfo("boot").kv("deploymentStage", deploymentSymbol).log("Normal boot");
                launchLifecycle();
                break;
            case deployment::DeploymentStage::BOOTSTRAP:
                LOG.atInfo("boot").kv("deploymentStage", deploymentSymbol).log("Resume deployment");
                launchBootstrap();
                break;
            case deployment::DeploymentStage::ROLLBACK_BOOTSTRAP:
                LOG.atInfo("boot").kv("deploymentStage", deploymentSymbol).log("Resume deployment");
                launchRollbackBootstrap();
                break;
            case deployment::DeploymentStage::KERNEL_ACTIVATION:
            case deployment::DeploymentStage::KERNEL_ROLLBACK:
                LOG.atInfo("boot").kv("deploymentStage", deploymentSymbol).log("Resume deployment");
                launchKernelDeployment();
                break;
            default:
                LOG.atError("deploymentStage")
                    .logAndThrow(
                        errors::BootError("Provided deployment stage at launch is not understood"));
        }
        try {
            // Return code is a boxed integer
            auto boxed = std::dynamic_pointer_cast<data::Boxed>(_mainPromise->getValue());
            return boxed->get().getInt();
        } catch(errors::PromiseNotFulfilledError &) {
            return 0;
        }
    }

    void Kernel::launchBootstrap() {
        throw std::runtime_error("TODO: launchBootstrap()");
    }

    void Kernel::launchRollbackBootstrap() {
        throw std::runtime_error("TODO: launchRollbackBootstrap()");
    }

    void Kernel::launchKernelDeployment() {
        throw std::runtime_error("TODO: launchKernelDeployment()");
    }

    void Kernel::launchLifecycle() {
        //
        // TODO: All of below is temporary logic - all this will be rewritten when the lifecycle
        // management is implemented.
        //
        _mainPromise = std::make_shared<pubsub::Promise>(context());

        auto &loader = context()->pluginLoader();
        loader.setPaths(getPaths());
        loader.setDeviceConfiguration(_deviceConfiguration);
        auto components = loader.discoverComponents();

        std::vector<std::string> names;
        names.reserve(components.size());
        for(const auto &recipe : components) {
            names.emplace_back(recipe.componentName);
        }
        auto future = _lifecycleManager->runComponents(names);
        future.get();

        std::filesystem::last_write_time("/home/").time_since_epoch().count();
        auto startupTopic = getConfig().lookupTopics(
            {"services", _deviceConfiguration->getNucleusComponentName(), "deplyOnStartup"});

        if(std::filesystem::path deploymentPath = startupTopic->lookup({"path"}).getString();
           !deploymentPath.empty() && std::filesystem::exists(deploymentPath)) {
            if(config::Timestamp{std::filesystem::last_write_time(deploymentPath)}
               > startupTopic->lookup({"lastModified"}).getModTime()) {
            }
        }

        // Block this thread until termination (TODO: improve on this somehow)
        _mainPromise->waitUntil(tasks::ExpireTime::infinite());

        // _lifecycleManager->stopAllComponents();

        getConfig().publishQueue().stop();
        _deploymentManager->stop();
        context()->logManager().publishQueue()->stop();
    }

    std::shared_ptr<config::Topics> Kernel::findServiceTopic(const std::string_view &serviceName) {
        if(!serviceName.empty()) {
            std::shared_ptr<config::ConfigNode> node =
                getConfig().root()->createInteriorChild(SERVICES_TOPIC_KEY)->getNode(serviceName);
            return std::dynamic_pointer_cast<config::Topics>(node);
        }
        return nullptr;
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

        // TODO: missing code
        softShutdown(timeoutSeconds);
        // Signal shutdown
        try {
            _mainPromise->setValue(data::Boxed::box(context(), exitCode));
        } catch(errors::PromiseDoubleWriteError &) {
            // ignore double-write
        }
    }

    void Kernel::softShutdown(std::chrono::seconds expireTime) {
        getConfig().publishQueue().drainQueue();
        _deploymentManager->clearQueue();
        LOG.atDebug("system-shutdown").log("Starting soft shutdown");
        stopAllServices(expireTime);
        LOG.atDebug("system-shutdown").log("Closing transaction log");
        _tlog->commit();
        writeEffectiveConfig();
    }
    config::Manager &Kernel::getConfig() {
        return context()->configManager();
    }

    std::vector<std::string> Kernel::getSupportedCapabilities() const {
        // TODO: This should be coming from GG SDK.
        std::ignore = *this;
        std::vector<std::string> v;
        return std::vector<std::string>{
            "LARGE_CONFIGURATION", "LINUX_RESOURCE_LIMITS", "SUB_DEPLOYMENTS"};
    }
} // namespace lifecycle
