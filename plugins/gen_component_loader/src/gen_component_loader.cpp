#include "gen_component_loader.hpp"
#include <c_api.h>
#include <containers.hpp>
#include <cstdlib>
#include <filesystem>
#include <gg_pal/process.hpp>
#include <handles.hpp>
#include <memory>
#include <mutex>
#include <regex>
#include <scopes.hpp>
#include <string>
#include <string_util.hpp>
#include <temp_module.hpp>
#include <utility>

static const auto LOG = ggapi::Logger::of("gen_component_loader");

static constexpr std::string_view on_path_prefix = "onpath";
static constexpr std::string_view exists_prefix = "exists";

void GenComponentDelegate::lifecycleCallback(
    const std::shared_ptr<GenComponentDelegate> &self,
    const ggapi::ModuleScope &,
    ggapi::Symbol event,
    ggapi::Struct data) {
    self->lifecycle(event, std::move(data));
}

ggapi::ModuleScope GenComponentDelegate::registerComponent() {
    // baseRef() enables the class to be able to point to itself
    auto module = ggapi::ModuleScope::registerGlobalPlugin(
        _name, ggapi::LifecycleCallback::of(&GenComponentDelegate::lifecycleCallback, baseRef()));
    return module;
}

static std::string getEnvVar(std::string_view variable) {
    // concurrent calls to getenv by itself does not introduce a data-race in C++11, as long as
    // functions modifying the host environment are not called.
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char *env = std::getenv(variable.data());
    if(env == nullptr) {
        return {};
    }
    return {env};
}

gg_pal::Process GenComponentDelegate::startProcess(
    std::string script,
    std::chrono::seconds timeout,
    bool requiresPrivilege,
    const gg_pal::EnvironmentMap &env,
    const std::string &note) {
    using namespace std::string_literals;

    auto getShell = [this]() -> std::string {
        auto posixShell =
            _nucleusConfig.getValue<std::string>({"configuration", "runWithDefault", "posixShell"});

        if(!posixShell.empty()) {
            return posixShell;
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

    // TODO:: get the actual value
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

        auto socketPath = result.hasKey("domain_socket_path")
                              ? std::make_optional(result.get<std::string>("domain_socket_path"))
                              : std::nullopt;
        auto authToken = result.hasKey("cli_auth_token")
                             ? std::make_optional(result.get<std::string>("cli_auth_token"))
                             : std::nullopt;
        return {std::move(socketPath), std::move(authToken)};
    }();

    // Here the scope for GenComponentDelagate isn't passed within the Startable
    // Hence a weak self pointing variable is required
    auto weak_self = std::weak_ptr(ref<GenComponentDelegate>());

    gg_pal::EnvironmentMap envExt{
        {"PATH", getEnvVar("PATH")},
        {"SVCUID", authToken},
        {"AWS_GG_NUCLEUS_DOMAIN_SOCKET_FILEPATH_FOR_COMPONENT"s, std::move(socketPath)},
        {"AWS_CONTAINER_CREDENTIALS_FULL_URI"s, std::move(container_uri)},
        {"AWS_CONTAINER_AUTHORIZATION_TOKEN"s, std::move(authToken)},
        {"AWS_IOT_THING_NAME"s, getThingName()},
        {"GG_ROOT_CA_PATH"s, getRootCAPath()},
        {"AWS_REGION"s, getAWSRegion()},
        {"AWS_DEFAULT_REGION"s, getAWSRegion()},
        {"GGC_VERSION"s, getNucleusVersion}};

    gg_pal::EnvironmentMap fullEnv{env};
    fullEnv.merge(envExt);

    auto [user, group] = [this, &requiresPrivilege]()
        -> std::pair<std::optional<std::string>, std::optional<std::string>> {
        if(requiresPrivilege) {
            return {"root", "root"};
        }
        auto cfg =
            _nucleusConfig.getValue<std::string>({"configuration", "runWithDefault", "posixUser"});
        if(cfg.empty()) {
            return {};
        }
        // TODO: Windows
        auto it = cfg.find(':');
        if(it == std::string::npos) {
            return {cfg, std::nullopt};
        }
        return {cfg.substr(0, it), cfg.substr(it + 1)};
    }();

    struct Ctx {
        std::mutex mtx;
        bool complete;
    };

    auto ctx = std::make_shared<Ctx>();

    gg_pal::Process proc = {
        getShell(),
        {"-c", std::move(script)},
        // TODO: configure proc cwd
        std::filesystem::current_path(),
        fullEnv,
        user,
        group,
        [note = note, weak_self](util::Span<const char> buffer) {
            auto self = weak_self.lock();
            if(!self) {
                return;
            }
            util::TempModule moduleScope(self->getModule());

            if(buffer.empty()) {
                return;
            }

            LOG.atInfo("stdout")
                .kv("note", note)
                .kv("message", std::string_view{buffer.data(), buffer.size()})
                .log();
        },
        [note = note, weak_self](util::Span<const char> buffer) {
            auto self = weak_self.lock();
            if(!self) {
                return;
            }
            util::TempModule moduleScope(self->getModule());

            if(buffer.empty()) {
                return;
            }

            LOG.atWarn("stderr")
                .kv("note", note)
                .kv("message", std::string_view{buffer.data(), buffer.size()})
                .log();
        },
        [weak_self, ctx](int returnCode) mutable {
            auto self = weak_self.lock();
            if(!self) {
                return;
            }
            util::TempModule moduleScope(self->getModule());

            std::lock_guard<std::mutex> guard(ctx->mtx);
            ctx->complete = true;

            if(returnCode == 0) {
                LOG.atInfo("process-exited").kv("returnCode", returnCode).log();
            } else {
                LOG.atError("process-failed").kv("returnCode", returnCode).log();
            }
        }};

    auto currentTimePoint = std::chrono::steady_clock::now();
    auto timeoutPoint = currentTimePoint + timeout;
    if(timeoutPoint != std::chrono::steady_clock::time_point::min()) {
        // TODO: Move timeout logic to lifecycle manager.
        // TODO: Fix miniscule time delay by doing conversion with timepoints. Keep timeout as
        // same unit throughout. (2s vs 1.9999s)
        auto currentTimePoint = std::chrono::steady_clock::now();
        auto duration =
            std::chrono::duration_cast<std::chrono::milliseconds>(timeoutPoint - currentTimePoint);
        auto delay = static_cast<uint32_t>(duration.count());

        ggapi::later(
            delay,
            [weak_self, ctx, note = note](gg_pal::Process proc) {
                std::lock_guard<std::mutex> guard(ctx->mtx);
                if(!ctx->complete) {
                    LOG.atWarn("process-timeout")
                        .kv("note", note)
                        .log("Process has reached the time out limit, stopping.");

                    static constexpr int killDelayMs = 5000;
                    ggapi::later(
                        killDelayMs,
                        [weak_self, ctx, note = note](gg_pal::Process proc) {
                            std::lock_guard<std::mutex> guard(ctx->mtx);
                            if(!ctx->complete) {
                                LOG.atWarn("process-stop-timeout")
                                    .kv("note", note)
                                    .log("Process failed to stop in time, killing.");

                                proc.kill();
                            }
                        },
                        proc);

                    proc.stop();
                }
            },
            proc);
    }

    return proc;
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

    auto process = std::invoke([&]() -> gg_pal::Process {
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
            auto script =
                std::regex_replace(step.script, std::regex(R"(\{artifacts:path\})"), _artifactPath);

            // if(_defaultConfig && !_defaultConfig.empty()) {
            //     for(auto key : _defaultConfig.keys().toVector<ggapi::Archive::KeyType>()) {
            //         auto value = _defaultConfig.get<std::string>(key);
            //         script = std::regex_replace(
            //             script,
            //             std::regex(R"(\{configuration:\/)" + key.toString() + R"(\})"),
            //             value);
            //     }
            // }

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

    if(process) {
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
    _recipeAsStruct = data.get<ggapi::Struct>("recipe");
    _manifestAsStruct = data.get<ggapi::Struct>("manifest");
    //TODO: fetch this information from nucleus's config
    _artifactPath = data.get<std::string>("artifactPath");

    _deploymentId = _recipeAsStruct.get<std::string>(_recipeAsStruct.foldKey("ComponentName"));
    auto compConfig = _recipeAsStruct.get<ggapi::Struct>(_recipeAsStruct.foldKey("ComponentConfiguration"));
    auto _defaultConfig = compConfig.get<ggapi::Struct>(compConfig.foldKey("DefaultConfiguration"));

    // TODO:: Improve how Lifecycle is extracted from recipe with respect to manifest
    _lifecycleAsStruct =
        _manifestAsStruct.get<ggapi::Struct>(_manifestAsStruct.foldKey("Lifecycle"));
}

void GenComponentDelegate::onInitialize(ggapi::Struct data) {
    data.put(NAME, "aws.greengrass.gen_component_delegate");

    _nucleusConfig = data.getValue<ggapi::Struct>({"nucleus"});
    _systemConfig = data.getValue<ggapi::Struct>({"system"});

    ggapi::Archive::transform<ggapi::ContainerDearchiver>(_lifecycle, _lifecycleAsStruct);

    if(_lifecycle.envMap.has_value()) {
        _globalEnv.insert(_lifecycle.envMap->begin(), _lifecycle.envMap->end());
    }

    if(_lifecycle.install.has_value()) {
        processScript(_lifecycle.install.value(), "install");
    }
}

void GenComponentDelegate::onStart(ggapi::Struct data) {
    if(_lifecycle.envMap.has_value()) {
        _globalEnv.insert(_lifecycle.envMap->begin(), _lifecycle.envMap->end());
    }

    if(_lifecycle.startup.has_value()) {
        processScript(_lifecycle.startup.value(), "startup");
    } else if(_lifecycle.run.has_value()) {
        processScript(_lifecycle.run.value(), "run");
    } else {
        // TODO:: Find a better LOG and throw
        throw ggapi::GgApiError("No deployment run or startup phase provided");
    }
}

ggapi::ObjHandle GenComponentLoader::registerGenComponent(
    ggapi::Symbol, const ggapi::Container &callData) {
    ggapi::Struct data{callData};

    auto newModule = std::make_shared<GenComponentDelegate>(data);

    // TODO:
    ggapi::Struct returnData = ggapi::Struct::create();

    auto module = newModule->registerComponent();

    returnData.put("moduleHandle", module);
    return returnData;
}

void GenComponentLoader::onInitialize(ggapi::Struct data) {

    data.put(NAME, "aws.greengrass.gen_component_loader");

    _delegateComponentSubscription = ggapi::Subscription::subscribeToTopic(
        ggapi::Symbol{"componentType::aws.greengrass.generic"},
        ggapi::TopicCallback::of(&GenComponentLoader::registerGenComponent, this));

    // TODO:Uncomment when kernel is ready with subscription
    //  Notify nucleus that this plugin supports loading generic components
    //  auto request{ggapi::Struct::create()};
    //  request.put("componentSupportType", "aws.greengrass.generic");
    //  request.put("componentSupportTopic", "componentType::aws.greengrass.generic");
    //  auto future =
    //      ggapi::Subscription::callTopicFirst(ggapi::Symbol{"aws.greengrass.componentType"},
    //      request);
    //  if(future.isValid()){
    //      auto response = ggapi::Struct(future.waitAndGetValue());
    //  }
}
