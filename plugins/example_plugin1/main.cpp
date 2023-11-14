#include <iostream>
#include <plugin.hpp>

class ExamplePlugin : public ggapi::Plugin {
public:
    void beforeLifecycle(ggapi::StringOrd phase, ggapi::Struct data) override;
    bool onStart(ggapi::Struct data) override;

    bool onRun(ggapi::Struct data) override;

    static ggapi::Struct testListener(
        ggapi::Task task, ggapi::StringOrd topic, ggapi::Struct callData
    );

    static ExamplePlugin &get() {
        static ExamplePlugin instance{};
        return instance;
    }
};

ggapi::Struct ExamplePlugin::testListener(ggapi::Task, ggapi::StringOrd, ggapi::Struct callData) {

    std::string pingMessage{callData.get<std::string>("ping")};
    ggapi::Struct response = ggapi::Struct::create();
    response.put("pong", pingMessage);
    return response;
}

bool ExamplePlugin::onStart(ggapi::Struct data) {
    std::ignore = getScope().subscribeToTopic(ggapi::StringOrd{"test"}, testListener);
    return true;
}

bool ExamplePlugin::onRun(ggapi::Struct data) {
    return true;
}

void ExamplePlugin::beforeLifecycle(ggapi::StringOrd phase, ggapi::Struct data) {
    std::cout << "Running lifecycle plugins 1... " << ggapi::StringOrd{phase}.toString()
              << std::endl;
}

bool greengrass_lifecycle(uint32_t moduleHandle, uint32_t phase, uint32_t data) noexcept {
    return ExamplePlugin::get().lifecycle(moduleHandle, phase, data);
}
