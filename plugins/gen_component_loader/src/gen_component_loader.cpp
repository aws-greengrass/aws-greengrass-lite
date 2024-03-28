#include "gen_component_loader.hpp"
#include "c_api.h"
#include "containers.hpp"
#include "handles.hpp"
#include "scopes.hpp"
#include "string_util.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
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
        _name, ggapi::LifecycleCallback::of(&GenComponentDelegate::lifecycleCallback, baseRef()));
    return module;
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
            if(lifecycle.envMap.has_value()) {
                localEnv.insert(lifecycle.envMap->begin(), lifecycle.envMap->end());
            }
            // Append global entries where local entries do not exist
            localEnv.insert(globalEnv.begin(), globalEnv.end());
            return localEnv;
        };

        // script
        auto getScript = [&]() -> std::string {
            auto script = std::regex_replace(
                step.script, std::regex(R"(\{artifacts:path\})"), artifactPath.string());

            if(defaultConfig && !defaultConfig->empty()) {
                for(auto key : defaultConfig->getKeys()) {
                    auto value = defaultConfig->get(key);
                    if(value.isScalar()) {
                        script = std::regex_replace(
                            script,
                            std::regex(R"(\{configuration:\/)" + key + R"(\})"),
                            value.getString());
                    }
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

        return _kernel.startProcess(
            getScript(), timeout, requirePrivilege, getSetEnv(), currentRecipe.componentName);
    });

    if(pid) {
        LOG.atInfo("deployment")
            .kv(DEPLOYMENT_ID_LOG_KEY, currentDeployment.id)
            .kv(GG_DEPLOYMENT_ID_LOG_KEY_NAME, currentDeployment.id)
            .kv("DeploymentType", "LOCAL")
            .log("Executed " + stepName + " step of the lifecycle");
    } else {
        LOG.atError("deployment")
            .kv(DEPLOYMENT_ID_LOG_KEY, currentDeployment.id)
            .kv(GG_DEPLOYMENT_ID_LOG_KEY_NAME, currentDeployment.id)
            .kv("DeploymentType", "LOCAL")
            .log("Failed to execute " + stepName + " step of the lifecycle");
        return; // if any of the lifecycle step fails, stop the deployment
    }
}

bool GenComponentDelegate::onInitialize(ggapi::Struct data) {
    data.put(NAME, "aws.greengrass.gen_component_delegate");

    auto lifecycleAsStruct = _recipe.get<ggapi::Struct>("Lifecycle");

    ggapi::Archive::transform<ggapi::ContainerDearchiver>(_lifecycle, lifecycleAsStruct);

    if(_lifecycle.envMap.has_value()) {
        _globalEnv.insert(_lifecycle.envMap->begin(), _lifecycle.envMap->end());
    }

    processScript(_lifecycle.install);
    std::cout << "I was initialized" << std::endl;
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

    // Notify nucleus that this plugin supports loading generic components
    auto request{ggapi::Struct::create()};
    request.put("componentSupportType", "aws.greengrass.generic");
    request.put("componentSupportTopic", "componentType::aws.greengrass.generic");
    auto future =
        ggapi::Subscription::callTopicFirst(ggapi::Symbol{"aws.greengrass.componentType"}, request);
    auto response = ggapi::Struct(future.waitAndGetValue());

    return true;
}
