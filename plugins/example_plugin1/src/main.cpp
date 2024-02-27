#include <iostream>
#include <plugin.hpp>

class ExamplePlugin : public ggapi::Plugin {
public:
    void beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) override;
    bool onStart(ggapi::Struct data) override;

    bool onRun(ggapi::Struct data) override;

    static ggapi::Struct testListener(
        ggapi::Task task, ggapi::Symbol topic, ggapi::Struct callData);

    static ExamplePlugin &get() {
        static ExamplePlugin instance{};
        return instance;
    }
};

ggapi::Struct ExamplePlugin::testListener(ggapi::Task, ggapi::Symbol, ggapi::Struct callData) {

    std::string pingMessage{callData.get<std::string>("ping")};
    ggapi::Struct response = ggapi::Struct::create();
    response.put("pong", pingMessage);
    return response;
}

bool ExamplePlugin::onStart(ggapi::Struct data) {
    std::ignore = getScope().subscribeToTopic(ggapi::Symbol{"test"}, testListener);
    return true;
}

bool ExamplePlugin::onRun(ggapi::Struct data) {
    return true;
}

void ExamplePlugin::beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) {
    std::cout << "Running lifecycle plugins 1... " << ggapi::Symbol{phase}.toString() << std::endl;
}

extern "C" [[maybe_unused]] ggapiErrorKind greengrass_lifecycle(
    ggapiObjHandle moduleHandle, ggapiSymbol phase, ggapiObjHandle data, bool *pHandled) noexcept {
    return ExamplePlugin::get().lifecycle(moduleHandle, phase, data, pHandled);
}
