#include "example_mqtt_sender.hpp"

static const Keys keys;

ggapi::Struct mqttListener(ggapi::Struct args) {

    std::string topic{args.get<std::string>(keys.topicName)};
    std::string payload{args.get<std::string>(keys.payload)};

    std::cout << "[example-mqtt-sender] Publish received on topic " << topic << ": " << payload
              << std::endl;
    auto response = ggapi::Struct::create();
    response.put("status", true);
    return response;
}

void MqttSender::beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) {
    ggapi::Symbol phaseOrd{phase};
    std::cerr << "[example-mqtt-sender] Running lifecycle phase " << phaseOrd.toString()
              << std::endl;
}

bool MqttSender::onStart(ggapi::Struct data) {
    return true;
}

bool MqttSender::onRun(ggapi::Struct data) {
    // subscribe to a topic
    auto request{ggapi::Struct::create()};
    request.put(keys.topicName, "ping/#");
    request.put(keys.qos, 1);

    // TODO: Use anonymous listener handle
    auto result = ggapi::Task::sendToTopic(keys.subscribeToIoTCoreTopic, request);
    if (!result.empty()) {
        auto channel = getScope().anchor(result.get<ggapi::Channel>(keys.channel));
        channel.addListenCallback(mqttListener);
        channel.addCloseCallback([channel]() { channel.release(); });
    }
    // publish to a topic on an async thread
    _asyncThread = std::thread{&MqttSender::threadFn, this};
    return true;
}

bool MqttSender::onTerminate(ggapi::Struct data) {
    std::cerr << "[example-mqtt-sender] Stopping publish thread..." << std::endl;
    _isRunning.store(false);
    _asyncThread.join();
    return true;
}

void MqttSender::threadFn() {
    std::cerr << "[example-mqtt-sender] Started publish thread" << std::endl;
    _isRunning = true;
    _cv.notify_all();
    while(_isRunning.load()) {
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
