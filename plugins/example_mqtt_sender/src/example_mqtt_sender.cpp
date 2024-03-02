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

    auto resultFuture = ggapi::Subscription::callTopicFirst(keys.subscribeToIoTCoreTopic, request);
    ggapi::Struct result;
    if(resultFuture) {
        // TODO: Make non-blocking
        result = ggapi::Struct(resultFuture.waitAndGetValue());
    }
    if(result && !result.empty()) {
        auto channel = result.get<ggapi::Channel>(keys.channel);
        channel.addListenCallback(mqttListener);
        channel.addCloseCallback([channel]() {});
    }
    // publish to a topic on an async thread
    _asyncThread = std::thread{&MqttSender::threadFn, this, getScope()};
    return true;
}

bool MqttSender::onTerminate(ggapi::Struct data) {
    std::cerr << "[example-mqtt-sender] Stopping publish thread..." << std::endl;
    _running = false;
    _asyncThread.join();
    return true;
}

void MqttSender::threadFn(const ggapi::ModuleScope &module) {
    std::ignore = module.setActive();
    std::cerr << "[example-mqtt-sender] Started publish thread" << std::endl;
    _running = true;
    _cv.notify_one();
    while(_running.load()) {
        auto request{ggapi::Struct::create()};
        request.put(keys.topicName, "hello");
        request.put(keys.qos, 1);
        request.put(keys.payload, "Hello world!");

        std::cerr << "[example-mqtt-sender] Sending..." << std::endl;
        auto sendFuture = ggapi::Subscription::callTopicFirst(keys.publishToIoTCoreTopic, request);
        if(sendFuture) {
            sendFuture.whenValid([](ggapi::Future) {
                std::cerr << "[example-mqtt-sender] Sending complete." << std::endl;
            });
        }

        using namespace std::chrono_literals;

        std::this_thread::sleep_for(5s);
    }
}
