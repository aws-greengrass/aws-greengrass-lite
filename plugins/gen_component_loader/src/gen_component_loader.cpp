#include "gen_component_loader.hpp"
#include "c_api.h"
#include "containers.hpp"
#include "handles.hpp"
#include "platform_abstraction/startable.hpp"
#include "scopes.hpp"
#include "string_util.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <temp_module.hpp>
#include <utility>

    static const auto LOG = ggapi::Logger::of("gen_component_loader");

    static constexpr std::string_view on_path_prefix = "onpath";
    static constexpr std::string_view exists_prefix = "exists";

    bool GenComponentDelegate::lifecycleCallback(
        const std::shared_ptr<GenComponentDelegate> &self,
        ggapi::ModuleScope,
        ggapi::Symbol event,
        ggapi::Struct data) {
        return self->lifecycle(event, std::move(data));
    }

    ggapi::ModuleScope GenComponentDelegate::registerComponent() {
        // baseRef() enables the class to be able to point to itself
        auto module = ggapi::ModuleScope::registerGlobalPlugin(
            _name,
            ggapi::LifecycleCallback::of(&GenComponentDelegate::lifecycleCallback, baseRef()));
        return module;
    }

    ipc::ProcessId GenComponentDelegate::startProcess(
        std::string script,
        std::chrono::seconds timeout,
        bool requiresPrivilege,
        std::unordered_map<std::string, std::optional<std::string>> env,
        const std::string &note,
        std::optional<ipc::CompletionCallback> onComplete) {
        using namespace std::string_literals;

        auto getShell = [this]() -> std::string {
            if(_nucleusConfig.getValue<std::string>(
                   {"configuration", "runWithDefault", "posixShell"})
               != "") {
                return _nucleusConfig.getValue<std::string>(
                    {"configuration", "runWithDefault", "posixShell"});
            } else {
                LOG.atWarn("missing-config-option")
                    .kv("message", "posixShell not configured. Defaulting to bash.")
                    .log();
                return "bash"s;
            }
        };

        auto getThingName = [this]() -> std::string {
            return _systemConfig.getValue<std::string>({"thingName"});
        };

        auto getAWSRegion = [this]() -> std::string {
            return _nucleusConfig.getValue<std::string>({"configuration", "awsRegion"});
        };

        auto getRootCAPath = [this]() -> std::string {
            return _systemConfig.getValue<std::string>({"rootpath"});
        };

        //TODO:: get the actual value
        std::string getNucleusVersion = "0.0.0";

        // TODO: query TES plugin
        std::string container_uri = "http://localhost:8090/2016-11-01/credentialprovider/";

        auto [socketPath, authToken] =
            [note = note]() -> std::pair<std::optional<std::string>, std::optional<std::string>> {
            auto request = ggapi::Struct::create();
            // TODO: is note correct here?
            request.put("serviceName", note);
            auto resultFuture =
                ggapi::Subscription::callTopicFirst("aws.greengrass.RequestIpcInfo", request);
            if(!resultFuture) {
                return {};
            };
            auto result = ggapi::Struct(resultFuture.waitAndGetValue());
            if(!result || result.empty()) {
                return {};
            }

            auto socketPath =
                result.hasKey("domain_socket_path")
                    ? std::make_optional(result.get<std::string>("domain_socket_path"))
                    : std::nullopt;
            auto authToken = result.hasKey("cli_auth_token")
                                 ? std::make_optional(result.get<std::string>("cli_auth_token"))
                                 : std::nullopt;
            return {std::move(socketPath), std::move(authToken)};
        }();
        
        //Here the scope for GenComponentDelagate isn't passed within the Startable 
        //Hence a weak self pointing variable is required
        auto weak_self = std::weak_ptr(ref<GenComponentDelegate>());
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
                .addEnvironment("GG_ROOT_CA_PATH"s, getRootCAPath())
                .addEnvironment("AWS_REGION"s, getAWSRegion())
                .addEnvironment("AWS_DEFAULT_REGION"s, getAWSRegion())
                .addEnvironment("GGC_VERSION"s, getNucleusVersion)
                // TODO: Windows "run raw script" switch
                .withArguments({"-c", std::move(script)})
                // TODO: allow output to pass back to caller if subscription is specified
                .withOutput([note = note, weak_self](util::Span<const char> buffer) {
                    auto self  = weak_self.lock();
                    if(!self){
                        return;
                    }
                    util::TempModule moduleScope(self->getModule());
                    LOG.atInfo("stdout")
                        .kv("note", note)
                        .kv("message", std::string_view{buffer.data(), buffer.size()})
                        .log();
                })
                .withError([note = note, weak_self](util::Span<const char> buffer) {
                    auto self  = weak_self.lock();
                    if(!self){
                        return;
                    }
                    util::TempModule moduleScope(self->getModule());

                    LOG.atWarn("stderr")
                        .kv("note", note)
                        .kv("message", std::string_view{buffer.data(), buffer.size()})
                        .log();
                })
                .withCompletion([onComplete = std::move(onComplete), weak_self](int returnCode) mutable {
                    auto self  = weak_self.lock();
                    if(!self){
                        return;
                    }
                    util::TempModule moduleScope(self->getModule());
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
                auto cfg = _nucleusConfig.getValue<std::string>(
                    {"configuration", "runWithDefault", "posixUser"});
                ;
                if(cfg == "") {
                    return {};
                }
                // TODO: Windows
                auto it = cfg.find(':');
                if(it == std::string::npos) {
                    return {cfg, std::nullopt};
                }
                return {cfg.substr(0, it), cfg.substr(it + 1)};
            }();
            if(user) {
                startable.asUser(std::move(user).value());
                if(group) {
                    startable.asGroup(std::move(group).value());
                }
            }
        } else {
            // requiresPrivilege -> run as root user
            startable.asUser("root");
            startable.asGroup("root");
        }
        return _manager.registerProcess(
            [this, startable]() -> std::unique_ptr<ipc::Process> {
                try {
                    return startable.start();
                } catch(const std::exception &e) {
                    LOG.atError().event("process-start-error").cause(e).log();
                    return {};
                }
            }());
    }

    void GenComponentDelegate::processScript(ScriptSection section, std::string_view stepNameArg) {
        using namespace std::chrono_literals;

        auto &step = section;
        std::string stepName{stepNameArg};

        // execute each lifecycle phase
        auto deploymentRequest = ggapi::Struct::create();

        if(step.skipIf.has_value()) {
            auto skipIf = util::splitWith(step.skipIf.value(), ' ');
            if(!skipIf.empty()) {
                std::string cmd = util::lower(skipIf[0]);
                // skip the step if the executable exists on path
                if(cmd == on_path_prefix) {
                    const auto &executable = skipIf[1];
                    // TODO: This seems so odd here? - code does nothing
                    auto envList = ggapi::List::create();
                    envList.put(0, executable);
                    auto request = ggapi::Struct::create();
                    request.put("GetEnv", envList);
                    // TODO: Skipif
                }
                // skip the step if the file exists
                else if(cmd == exists_prefix) {
                    if(std::filesystem::exists(skipIf[1])) {
                        return;
                    }
                }
                // TODO: what if sub-command not recognized?
            }
        }

        auto pid = std::invoke([&]() -> ipc::ProcessId {
            // TODO: This needs a cleanup

            auto getSetEnv = [&]() -> std::unordered_map<std::string, std::optional<std::string>> {
                Environment localEnv;
                if(_lifecycle.envMap.has_value()) {
                    localEnv.insert(_lifecycle.envMap->begin(), _lifecycle.envMap->end());
                }
                // Append global entries where local entries do not exist
                localEnv.insert(_globalEnv.begin(), _globalEnv.end());
                return localEnv;
            };

            // script
            auto getScript = [&]() -> std::string {
                auto script = std::regex_replace(
                    step.script, std::regex(R"(\{artifacts:path\})"), _artifactPath.string());

                if(_defaultConfig && !_defaultConfig.empty()) {
                    for(auto key : _defaultConfig.keys().toVector<ggapi::Archive::KeyType>()) {
                        auto value = _defaultConfig.get<std::string>(key);
                        script = std::regex_replace(
                            script,
                            std::regex(R"(\{configuration:\/)" + key.toString() + R"(\})"),
                            value);
                    }
                }

                return script;
            };

            bool requirePrivilege = false;
            // TODO: default should be *no* timeout
            static constexpr std::chrono::seconds DEFAULT_TIMEOUT{120};
            std::chrono::seconds timeout = DEFAULT_TIMEOUT;

            // privilege
            if(step.requiresPrivilege.has_value() && step.requiresPrivilege.value()) {
                requirePrivilege = true;
                deploymentRequest.put("RequiresPrivilege", requirePrivilege);
            }

            // timeout
            if(step.timeout.has_value()) {
                deploymentRequest.put("Timeout", step.timeout.value());
                timeout = std::chrono::seconds{step.timeout.value()};
            }
            return startProcess(getScript(), timeout, requirePrivilege, getSetEnv(), _name);
        });

        if(pid) {
            LOG.atInfo("deployment")
                .kv(DEPLOYMENT_ID_LOG_KEY, _deploymentId)
                .kv(GG_DEPLOYMENT_ID_LOG_KEY_NAME, _deploymentId)
                .kv("DeploymentType", "LOCAL")
                .log("Executed " + stepName + " step of the lifecycle");
        } else {
            LOG.atError("deployment")
                .kv(DEPLOYMENT_ID_LOG_KEY, _deploymentId)
                .kv(GG_DEPLOYMENT_ID_LOG_KEY_NAME, _deploymentId)
                .kv("DeploymentType", "LOCAL")
                .log("Failed to execute " + stepName + " step of the lifecycle");
            return; // if any of the lifecycle step fails, stop the deployment
        }
    }
    GenComponentDelegate::GenComponentDelegate(const ggapi::Struct &data) {
        _name = data.get<std::string>("componentName");
        _recipe = data.get<ggapi::Struct>("recipe");
        _lifecycleAsStruct = data.get<ggapi::Struct>("lifecycle");
        _deploymentId = data.get<std::string>("deploymentId");
        _artifactPath = data.get<std::string>("artifactPath");
        _defaultConfig = data.get<ggapi::Struct>("defaultConfig");
    }

    bool GenComponentDelegate::onInitialize(ggapi::Struct data) {
        data.put(NAME, "aws.greengrass.gen_component_delegate");

        _nucleusConfig = data.getValue<ggapi::Struct>({"nucleus"});
        _systemConfig = data.getValue<ggapi::Struct>({"system"});

        ggapi::Archive::transform<ggapi::ContainerDearchiver>(_lifecycle, _lifecycleAsStruct);

        if(_lifecycle.envMap.has_value()) {
            _globalEnv.insert(_lifecycle.envMap->begin(), _lifecycle.envMap->end());
        }

        if (_lifecycle.install.has_value())
        {
           processScript(_lifecycle.install.value(), "install");
        }

        std::cout << "I was initialized" << std::endl;

        return true;
    }

    bool GenComponentDelegate::onStart(ggapi::Struct data) {
        if(_lifecycle.envMap.has_value()) {
            _globalEnv.insert(_lifecycle.envMap->begin(), _lifecycle.envMap->end());
        }

        if (_lifecycle.startup.has_value())
        {
           processScript(_lifecycle.startup.value(), "startup");
        }
        else if (_lifecycle.run.has_value())
        {
            processScript(_lifecycle.run.value(), "run");
        }
        else{
            //TODO:: Find a better LOG and throw
            throw ggapi::GgApiError("No deployment run or startup phase provided");
        }
        
        std::cout << "I have completed ontart" << std::endl;

        return true;
    }

    ggapi::ObjHandle GenComponentLoader::registerGenComponent(
        ggapi::Symbol, const ggapi::Container &callData) {
        ggapi::Struct data{callData};

        auto newModule = std::make_shared<GenComponentDelegate>(data);

        // TODO:
        ggapi::Struct returnData = ggapi::Struct::create();

        auto module = newModule->registerComponent();

        returnData.put("moduleHandle", module);
        return std::move(returnData);
    }

    bool GenComponentLoader::onInitialize(ggapi::Struct data) {

        data.put(NAME, "aws.greengrass.gen_component_loader");

        _delegateComponentSubscription = ggapi::Subscription::subscribeToTopic(
            ggapi::Symbol{"componentType::aws.greengrass.generic"},
            ggapi::TopicCallback::of(&GenComponentLoader::registerGenComponent, this));

        //TODO:Uncomment when kernel is ready with subscription
        // Notify nucleus that this plugin supports loading generic components
        // auto request{ggapi::Struct::create()};
        // request.put("componentSupportType", "aws.greengrass.generic");
        // request.put("componentSupportTopic", "componentType::aws.greengrass.generic");
        // auto future =
        //     ggapi::Subscription::callTopicFirst(ggapi::Symbol{"aws.greengrass.componentType"},
        //     request);
        // if(future.isValid()){
        //     auto response = ggapi::Struct(future.waitAndGetValue());
        // }
        return true;
    }
