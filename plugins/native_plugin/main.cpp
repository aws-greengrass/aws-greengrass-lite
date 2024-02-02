#include "cpp_api.hpp"
#include "plugin.hpp"

#include <exception>
#include <iostream>
#include <sstream>

#include "startable.hpp"

struct Keys {
    ggapi::Symbol serviceName{"serviceName"};
    ggapi::Symbol startProcessTopic{"aws.greengrass.Native.StartProcess"};
};

static const Keys keys;

const auto LOG = // NOLINT(cert-err58-cpp)
    ggapi::Logger::of("com.aws.greengrass.lifecycle.CommandLine");

class NativePlugin : public ggapi::Plugin {
    std::atomic<ggapi::Struct> _system;
    std::atomic<ggapi::Struct> _nucleus;
    static constexpr auto SERVICE_NAME = "aws.greengrass.Native";

public:
    void beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) override;
    bool onBind(ggapi::Struct data) override;
    bool onStart(ggapi::Struct data) override;
    bool onRun(ggapi::Struct data) override;
    bool onBootstrap(ggapi::Struct structData) override;

    ggapi::Struct testListener(ggapi::Task task, ggapi::Symbol topic, ggapi::Struct callData);

    static NativePlugin &get() {
        static NativePlugin instance{};
        return instance;
    }
};

ggapi::Struct NativePlugin::testListener(ggapi::Task, ggapi::Symbol, ggapi::Struct callData) {
    auto requiresPrivilege = callData.get<bool>("requiresPrivilege");
    auto script = callData.get<std::string>("script");
    auto workDir = callData.get<std::string>("workDir");
    if(!requiresPrivilege) {
        // SECURITY-TODO: Retrieve Change user
    }
    std::stringstream stream;
    stream << script;
    while(std::getline(stream, script)) {
        std::stringstream ss(script);
        std::vector<std::string> arguments;
        std::string token;
        while(std::getline(ss, token, ' ')) {
            arguments.emplace_back(std::move(token));
        }
        std::string frontCommand = arguments.front();
        arguments.erase(arguments.begin());
        try {
            ipc::Startable{}.WithCommand(frontCommand).WithArguments(arguments).Start();
        } catch(const std::exception &e) {
            LOG.atError().event("process-start-error").log(e.what());
        }
    }
    auto response = ggapi::Struct::create();
    response.put("status", true);
    return response;
}

bool NativePlugin::onBind(ggapi::Struct data) {
    _system = getScope().anchor(data.getValue<ggapi::Struct>({"system"}));
    _nucleus = getScope().anchor(data.getValue<ggapi::Struct>({"nucleus"}));
    return true;
}

bool NativePlugin::onStart(ggapi::Struct data) {
    auto nucleusConfig = _nucleus.load();
    auto userGroup =
        nucleusConfig.getValue<std::string>({"configuration", "runWithDefault", "posixUser"});
    if(!userGroup.empty()) {
        auto it = userGroup.find(":");
        auto userName = userGroup.substr(0, it);
        auto groupName = it != std::string::npos ? userGroup.substr(it + 1) : "";
    }
    std::ignore = getScope().subscribeToTopic(
        keys.startProcessTopic, ggapi::TopicCallback::of(&NativePlugin::testListener, this));
    return true;
}

bool NativePlugin::onRun(ggapi::Struct data) {
    auto request = ggapi::Struct::create();
    request.put(keys.serviceName, SERVICE_NAME);
    return true;
}

bool NativePlugin::onBootstrap(ggapi::Struct structData) {
    structData.put(NAME, SERVICE_NAME);
    return true;
}

void NativePlugin::beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) {
    std::cout << "[native-plugin] Running lifecycle phase " << phase.toString() << std::endl;
}

extern "C" [[maybe_unused]] ggapiErrorKind greengrass_lifecycle(
    ggapiObjHandle moduleHandle,
    ggapiSymbol phase,
    ggapiObjHandle data,
    bool *pWasHandled) noexcept {
    return NativePlugin::get().lifecycle(moduleHandle, phase, data, pWasHandled);
}
