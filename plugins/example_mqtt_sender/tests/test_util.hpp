#pragma once
#include "example_mqtt_sender.hpp"
#include "util.hpp"

static const Keys keys;

constexpr std::string_view BOOTSTRAP = "bootstrap";
constexpr std::string_view BIND = "bind";
constexpr std::string_view DISCOVER = "discover";
constexpr std::string_view START = "start";
constexpr std::string_view RUN = "run";
constexpr std::string_view TERMINATE = "terminate";


class TestExampleMqttSender : public ExampleMqttSender {
    ggapi::ModuleScope _moduleScope;
public:
    explicit TestExampleMqttSender(ggapi::ModuleScope moduleScope) : ExampleMqttSender(), _moduleScope(moduleScope) {

    }

    bool executePhase(std::string_view phase) {
        // TODO: Return before afterlifecycle?
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

    ggapi::Struct getPublishMessage() {
        return _publishMessage.load();
    }

    ggapi::Struct getSubscribeMessage() {
        return _subscribeMessage;
    }

};
