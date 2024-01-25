#include "cpp_api.hpp"
#include "plugin.hpp"

#include <exception>
#include <filesystem>
#include <iostream>

#include "startable.hpp"

class NativePlugin : public ggapi::Plugin {
public:
    void beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) override;
    bool onStart(ggapi::Struct data) override;

    bool onRun(ggapi::Struct data) override;
    bool onBootstrap(ggapi::Struct structData) override;

    static ggapi::Struct testListener(
        ggapi::Task task, ggapi::Symbol topic, ggapi::Struct callData);

    static NativePlugin &get() {
        static NativePlugin instance{};
        return instance;
    }
};

ggapi::Struct NativePlugin::testListener(ggapi::Task, ggapi::Symbol, ggapi::Struct callData) {

    std::string pingMessage{callData.get<std::string>("ping")};
    ggapi::Struct response = ggapi::Struct::create();
    response.put("pong", pingMessage);
    return response;
}

bool NativePlugin::onStart(ggapi::Struct data) {
    std::ignore = getScope().subscribeToTopic(ggapi::Symbol{"StartProcess"}, testListener);

    return true;
}

bool NativePlugin::onRun(ggapi::Struct data) {
    ipc::Startable{}.WithCommand("ls").WithArguments({"-l", "../"}).Start();
    return true;
}

bool NativePlugin::onBootstrap(ggapi::Struct structData) {
    structData.put(NAME, "aws.greengrass.native_plugin");
    return true;
}

void NativePlugin::beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) {
    std::cout << "Running lifecycle plugins 1... " << phase.asInt() << std::endl;
}

extern "C" [[maybe_unused]] ggapiErrorKind greengrass_lifecycle(
    ggapiObjHandle moduleHandle,
    ggapiSymbol phase,
    ggapiObjHandle data,
    bool *pWasHandled) noexcept {
    return NativePlugin::get().lifecycle(moduleHandle, phase, data, pWasHandled);
}
