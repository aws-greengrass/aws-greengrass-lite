#include <chrono>
#include <iostream>
#include <plugin.hpp>
#include <thread>

struct Keys {
    ggapi::StringOrd publishToIoTCoreTopic{"aws.greengrass.PublishToIoTCore"};
    ggapi::StringOrd subscribeToIoTCoreTopic{"aws.greengrass.SubscribeToIoTCore"};
    ggapi::StringOrd topicName{"topicName"};
    ggapi::StringOrd topicFilter{"topicFilter"};
    ggapi::StringOrd qos{"qos"};
    ggapi::StringOrd payload{"payload"};
    ggapi::StringOrd mqttPing{"mqttPing"};
    ggapi::StringOrd lpcResponseTopic{"lpcResponseTopic"};
};

class MqttSender : public ggapi::Plugin {
public:
    bool onStart(ggapi::Struct data) override;
    bool onRun(ggapi::Struct data) override;
    bool onTerminate(ggapi::Struct data) override;
    void beforeLifecycle(ggapi::StringOrd phase, ggapi::Struct data) override;

    static MqttSender &get() {
        static MqttSender instance{};
        return instance;
    }
};

static const Keys keys;

void threadFn();

ggapi::Struct mqttListener(ggapi::Task task, ggapi::StringOrd, ggapi::Struct args) {
    std::string topic{args.get<std::string>(keys.topicName)};
    std::string payload{args.get<std::string>(keys.payload)};

    std::cout << "[example-mqtt-sender] Publish recieved on topic " << topic << ": " << payload
              << std::endl;

    return ggapi::Struct::create();
}

extern "C" [[maybe_unused]] bool greengrass_lifecycle(
    uint32_t moduleHandle, uint32_t phase, uint32_t data) noexcept {
    return MqttSender::get().lifecycle(moduleHandle, phase, data);
}

void MqttSender::beforeLifecycle(ggapi::StringOrd phase, ggapi::Struct data) {
    ggapi::StringOrd phaseOrd{phase};
    std::cerr << "[example-mqtt-sender] Running lifecycle phase " << phaseOrd.toString()
              << std::endl;
}

bool MqttSender::onStart(ggapi::Struct data) {
    std::ignore = getScope().subscribeToTopic(keys.mqttPing, mqttListener);
    return true;
}

bool MqttSender::onRun(ggapi::Struct data) {
    auto request{ggapi::Struct::create()};
    request.put(keys.topicFilter, "ping/#");
    request.put(keys.qos, 1);
    // TODO: Use anonymous listener handle
    request.put(keys.lpcResponseTopic, keys.mqttPing);
    std::ignore = ggapi::Task::sendToTopic(keys.subscribeToIoTCoreTopic, request);

    std::thread asyncThread{threadFn};
    asyncThread.detach();
    return true;
}

bool MqttSender::onTerminate(ggapi::Struct data) {
    // TODO: signal asyncThread to terminate
    return true;
}

void threadFn() {
    std::cerr << "[example-mqtt-sender] Started thread" << std::endl;

    while(true) {
        ggapi::CallScope iterScope; // localize all structures
        auto request{ggapi::Struct::create()};
        request.put(keys.topicName, "hello");
        request.put(keys.qos, 1);
        request.put(keys.payload, "Hello world!");

        std::cerr << "[example-mqtt-sender] Sending..." << std::endl;
        ggapi::Struct result = ggapi::Task::sendToTopic(keys.publishToIoTCoreTopic, request);
        std::cerr << "[example-mqtt-sender] Sending complete." << std::endl;

        using namespace std::chrono_literals;

        std::this_thread::sleep_for(5s);
    }
}
