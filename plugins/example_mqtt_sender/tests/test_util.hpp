#pragma once
#include "example_mqtt_sender.hpp"

static const Keys keys;

constexpr std::string_view BOOTSTRAP = "bootstrap";
constexpr std::string_view BIND = "bind";
constexpr std::string_view DISCOVER = "discover";
constexpr std::string_view START = "start";
constexpr std::string_view RUN = "run";
constexpr std::string_view TERMINATE = "terminate";

class TestMqttSender : public MqttSender {
    ggapi::ModuleScope _moduleScope;

public:
    explicit TestMqttSender(ggapi::ModuleScope moduleScope)
        : MqttSender(), _moduleScope(moduleScope) {
    }

    bool executePhase(std::string_view phase) {
        // TODO: Return before afterLifecycle?
        beforeLifecycle(ggapi::Symbol{phase}, ggapi::Struct::create());
        bool status = lifecycle(_moduleScope, ggapi::Symbol{phase}, ggapi::Struct::create());
        afterLifecycle(ggapi::Symbol{phase}, ggapi::Struct::create());
        return status;
    };

    bool startLifecycle() {
        // TODO: gotta be a better way to do this
        return executePhase("start") && executePhase("run");
    }

    bool stopLifecycle() {
        return executePhase("terminate");
    }

    void wait() {
        std::unique_lock<std::mutex> lock(_mtx);
        _cv.wait(lock, [this]{return _isRunning.load();});
        using namespace std::chrono_literals;
        std::this_thread::sleep_for(0.5s);
    }
};

class PubSubCallback {
public:
    virtual ~PubSubCallback() = default;
    virtual ggapi::Struct publishHandler(ggapi::Task, ggapi::Symbol, ggapi::Struct) = 0;
    virtual ggapi::Struct subscribeHandler(ggapi::Task, ggapi::Symbol, ggapi::Struct) = 0;
};
