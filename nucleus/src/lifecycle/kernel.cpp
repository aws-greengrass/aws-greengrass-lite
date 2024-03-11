#include "kernel.hpp"
#include "command_line.hpp"
#include "config/yaml_config.hpp"
#include "deployment/device_configuration.hpp"
#include "logging/log_queue.hpp"
#include "platform_abstraction/abstract_process.hpp"
#include "platform_abstraction/abstract_process_manager.hpp"
#include "platform_abstraction/startable.hpp"
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

    Kernel::Kernel(const scope::UsingContext &context) : scope::UsesContext(context) {
        _nucleusPaths = std::make_shared<util::NucleusPaths>();
        _deploymentManager =
            std::make_unique<deployment::DeploymentManager>(scope::context(), *this);
        data::SymbolInit::init(context, {&SERVICES_TOPIC_KEY});
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
        initializeProcessManager(commandLine);
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

    void Kernel::initializeProcessManager(CommandLine &commandLine) {
        _processManager = std::make_unique<ipc::ProcessManager>();
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
        if(!_mainThread) {
            _mainThread.claim(std::make_shared<tasks::FixedTimerTaskThread>(context()));
        }
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
        _mainThread.release();
        return _exitCode;
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

        auto &loader = context()->pluginLoader();
        loader.setPaths(getPaths());
        loader.setDeviceConfiguration(_deviceConfiguration);
        loader.discoverPlugins(getPaths()->pluginPath());

        loader.forAllPlugins([&](plugins::AbstractPlugin &plugin, auto &data) {
            plugin.lifecycle(loader.DISCOVER, data);
        });
        loader.forAllPlugins([&](plugins::AbstractPlugin &plugin, auto &data) {
            plugin.lifecycle(loader.START, data);
        });
        loader.forAllPlugins([&](plugins::AbstractPlugin &plugin, auto &data) {
            plugin.lifecycle(loader.RUN, data);
        });

        std::ignore = ggapiWaitForTaskCompleted(
            ggapiGetCurrentTask(), -1); // essentially blocks until kernel signalled to terminate
        loader.forAllPlugins([&](plugins::AbstractPlugin &plugin, auto &data) {
            plugin.lifecycle(loader.TERMINATE, data);
        });
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
        setExitCode(exitCode);
        shutdown(timeoutSeconds);
    }

    void Kernel::shutdown(std::chrono::seconds timeoutSeconds) {
        // TODO: missing code
        softShutdown(timeoutSeconds);
        // Cancel the main task causes the main thread to terminate, causing clean shutdown
        _mainThread.getTask()->cancelTask();
    }

    void Kernel::softShutdown(std::chrono::seconds timeoutSeconds) {
        getConfig().publishQueue().drainQueue();
        _deploymentManager->clearQueue();
        LOG.atDebug("system-shutdown").log("Starting soft shutdown");
        stopAllServices(timeoutSeconds);
        LOG.atDebug("system-shutdown").log("Closing transaction log");
        _tlog->commit();
        writeEffectiveConfig();
    }
    config::Manager &Kernel::getConfig() {
        return context()->configManager();
    }

    ipc::ProcessId Kernel::startProcess(
        std::string script,
        std::chrono::seconds timeout,
        bool requiresPrivilege,
        std::unordered_map<std::string, std::optional<std::string>> env,
        const std::string &note,
        std::optional<ipc::CompletionCallback> onComplete) {
        using namespace std::string_literals;

        auto getShell = [this]() -> std::string {
            if(_deviceConfiguration->getRunWithDefaultPosixShell().isScalar()) {
                return _deviceConfiguration->getRunWithDefaultPosixShell().getString();
            } else {
                LOG.atWarn("missing-config-option")
                    .kv("message", "posixShell not configured. Defaulting to bash.")
                    .log();
                return "bash"s;
            }
        };

        auto getThingName = [this]() -> std::string {
            return _deviceConfiguration->getThingName().getString();
        };

        // TODO: query TES plugin
        std::string container_uri = "http://localhost:8090/2016-11-01/credentialprovider/";

        auto [socketPath, authToken] =
            [note = note]() -> std::pair<std::optional<std::string>, std::optional<std::string>> {
            auto request = ggapi::Struct::create();
            // TODO: is note correct here?
            request.put("serviceName", note);
            auto result = ggapi::Task::sendToTopic("aws.greengrass.RequestIpcInfo", request);
            if(!result || result.empty()) {
                return {};
            };

            auto socketPath =
                result.hasKey("domain_socket_path")
                    ? std::make_optional(result.get<std::string>("domain_socket_path"))
                    : std::nullopt;
            auto authToken = result.hasKey("cli_auth_token")
                                 ? std::make_optional(result.get<std::string>("cli_auth_token"))
                                 : std::nullopt;
            return {std::move(socketPath), std::move(authToken)};
        }();

        auto startable =
            ipc::Startable{}
                .withCommand(getShell())
                .withEnvironment(std::move(env))
                // TODO: Should entire nucleus env be copied?
                .addEnvironment(ipc::PATH_ENVVAR, ipc::getEnviron(ipc::PATH_ENVVAR))
                .addEnvironment("SVCUID", authToken)
                .addEnvironment(
                    "AWS_GG_NUCLEUS_DOMAIN_SOCKET_FILEPATH_FOR_COMPONENT"s, std::move(socketPath))
                .addEnvironment("AWS_CONTAINER_CREDENTIALS_FULL_URI"s, std::move(container_uri))
                .addEnvironment("AWS_CONTAINER_AUTHORIZATION_TOKEN"s, std::move(authToken))
                .addEnvironment("AWS_IOT_THING_NAME"s, getThingName())
                // TODO: Windows "run raw script" switch
                .withArguments({"-c", std::move(script)})
                // TODO: allow output to pass back to caller if subscription is specified
                .withOutput([note = note](util::Span<const char> buffer) {
                    LOG.atInfo("stdout")
                        .kv("note", note)
                        .kv("message", std::string_view{buffer.data(), buffer.size()})
                        .log();
                })
                .withError([note = note](util::Span<const char> buffer) {
                    LOG.atWarn("stderr")
                        .kv("note", note)
                        .kv("message", std::string_view{buffer.data(), buffer.size()})
                        .log();
                })
                .withCompletion([onComplete = std::move(onComplete)](int returnCode) mutable {
                    if(returnCode == 0) {
                        LOG.atInfo("process-exited").kv("returnCode", returnCode).log();
                    } else {
                        LOG.atError("process-failed").kv("returnCode", returnCode).log();
                    }
                    if(onComplete) {
                        (*onComplete)(returnCode == 0);
                    }
                });

        if(!requiresPrivilege) {
            auto [user, group] =
                [this]() -> std::pair<std::optional<std::string>, std::optional<std::string>> {
                auto cfg = _deviceConfiguration->getRunWithDefaultPosixUser();
                if(!cfg) {
                    return {};
                }
                // TODO: Windows
                auto str = cfg.getString();
                auto it = str.find(':');
                if(it == std::string::npos) {
                    return {str, std::nullopt};
                }
                return {str.substr(0, it), str.substr(it + 1)};
            }();
            if(user) {
                startable.asUser(std::move(user).value());
                if(group) {
                    startable.asGroup(std::move(group).value());
                }
            }
        }

        return _processManager->registerProcess([startable]() -> std::unique_ptr<ipc::Process> {
            try {
                return startable.start();
            } catch(const std::exception &e) {
                LOG.atError().event("process-start-error").cause(e).log();
                return {};
            }
        }());
    }
} // namespace lifecycle
