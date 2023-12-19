#include "example_mqtt_sender.hpp"

static const Keys keys;

ggapi::Struct ExampleMqttSender::mqttListener(ggapi::Task, ggapi::Symbol, ggapi::Struct args) {

    std::string topic{args.get<std::string>(keys.topicName)};
    std::string payload{args.get<std::string>(keys.payload)};

    std::cout << "[example-mqtt-sender] Publish received on topic " << topic << ": " << payload
              << std::endl;
    auto response = ggapi::Struct::create();
    response.put("status", true);
    return response;
}

void ExampleMqttSender::beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) {
    ggapi::Symbol phaseOrd{phase};
    std::cerr << "[example-mqtt-sender] Running lifecycle phase " << phaseOrd.toString()
              << std::endl;
}

bool ExampleMqttSender::onStart(ggapi::Struct data) {
    std::ignore = getScope().subscribeToTopic(
        keys.mqttPing, ggapi::TopicCallback::of(&ExampleMqttSender::mqttListener, this));
    return true;
}

bool ExampleMqttSender::onRun(ggapi::Struct data) {
    // publish to a topic on an async thread
    _asyncThread = std::thread{&ExampleMqttSender::threadFn, this};

    // subscribe to a topic
    auto request{ggapi::Struct::create()};
    request.put(keys.topicFilter, "ping/#");
    request.put(keys.qos, 1);
    // TODO: Use anonymous listener handle
    request.put(keys.lpcResponseTopic, keys.mqttPing);
    std::ignore = ggapi::Task::sendToTopic(keys.subscribeToIoTCoreTopic, request);
    return true;
}

bool ExampleMqttSender::onTerminate(ggapi::Struct data) {
    std::cerr << "[example-mqtt-sender] Stopping publish thread..." << std::endl;
    _running.store(false);
    _asyncThread.join();
    return true;
}

void ExampleMqttSender::threadFn() {
    std::cerr << "[example-mqtt-sender] Started publish thread" << std::endl;

    while(_running.load()) {
        ggapi::CallScope iterScope; // localize all structures
        auto request{ggapi::Struct::create()};
        request.put(keys.topicName, "hello");
        request.put(keys.qos, 1);
        request.put(keys.payload, "Hello world!");

        std::cerr << "[example-mqtt-sender] Sending..." << std::endl;
        std::ignore = ggapi::Task::sendToTopic(keys.publishToIoTCoreTopic, request);
        std::cerr << "[example-mqtt-sender] Sending complete." << std::endl;

        using namespace std::chrono_literals;

        std::this_thread::sleep_for(5s);
    }
}
