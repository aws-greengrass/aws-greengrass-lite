#include <chrono>
#include <cpp_api.hpp>
#include <iostream>
#include <thread>

struct Keys {
    ggapi::StringOrd start{"start"};
    ggapi::StringOrd run{"run"};
    ggapi::StringOrd publishToIoTCoreTopic{"aws.greengrass.PublishToIoTCore"};
    ggapi::StringOrd subscribeToIoTCoreTopic{"aws.greengrass.SubscribeToIoTCore"};
    ggapi::StringOrd topicName{"topicName"};
    ggapi::StringOrd topicFilter{"topicFilter"};
    ggapi::StringOrd qos{"qos"};
    ggapi::StringOrd payload{"payload"};
    ggapi::StringOrd mqttPing{"mqttPing"};
    ggapi::StringOrd lpcResponseTopic{"lpcResponseTopic"};
};

static const Keys keys;

void threadFn();

ggapi::Struct mqttListener(ggapi::Scope task, ggapi::StringOrd, ggapi::Struct args) {
    std::string topic{args.get<std::string>(keys.topicName)};
    std::string payload{args.get<std::string>(keys.payload)};

    std::cout << "[example-mqtt-sender] Publish recieved on topic " << topic << ": " << payload
              << std::endl;

    return task.createStruct();
}

extern "C" bool greengrass_lifecycle(
    uint32_t moduleHandle, uint32_t phase, uint32_t data
) noexcept {
    ggapi::StringOrd phaseOrd{phase};
    std::cerr << "[example-mqtt-sender] Running lifecycle phase " << phaseOrd.toString()
              << std::endl;
    if(phaseOrd == keys.start) {
        (void) ggapi::Scope::thisTask().subscribeToTopic(keys.mqttPing, mqttListener);
    } else if(phaseOrd == keys.run) {
        auto task{ggapi::Scope::thisTask()};
        auto request{task.createStruct()};
        request.put(keys.topicFilter, "ping/#");
        request.put(keys.qos, 1);
        request.put(keys.lpcResponseTopic, keys.mqttPing.toOrd());
        (void) task.sendToTopic(keys.subscribeToIoTCoreTopic, request);

        std::thread asyncThread{threadFn};
        asyncThread.detach();
    }
    return true;
}

void threadFn() {
    std::cerr << "[example-mqtt-sender] Started thread" << std::endl;
    auto threadScope = ggapi::ThreadScope::claimThread();

    while(true) {
        auto request{threadScope.createStruct()};
        request.put(keys.topicName, "hello");
        request.put(keys.qos, 1);
        request.put(keys.payload, "Hello world!");

        std::cerr << "[example-mqtt-sender] Sending..." << std::endl;
        ggapi::Struct result = threadScope.sendToTopic(keys.publishToIoTCoreTopic, request);
        std::cerr << "[example-mqtt-sender] Sending complete." << std::endl;

        using namespace std::chrono_literals;

        std::this_thread::sleep_for(5s);
    }
}
