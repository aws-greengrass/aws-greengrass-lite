#include <chrono>
#include <cpp_api.hpp>
#include <iostream>
#include <thread>

struct Keys {
    ggapi::StringOrd start{"start"};
    ggapi::StringOrd run{"run"};
    ggapi::StringOrd publishToIoTCoreTopic{"aws.greengrass.PublishToIoTCore"};
    ggapi::StringOrd topicName{"topicName"};
    ggapi::StringOrd qos{"qos"};
    ggapi::StringOrd payload{"payload"};
};

static const Keys keys;

void threadFn();

extern "C" bool greengrass_lifecycle(uint32_t moduleHandle, uint32_t phase, uint32_t data) noexcept {
    ggapi::StringOrd phaseOrd{phase};
    std::cout << "[sample-mqtt-user] Running lifecycle phase " << phaseOrd.toString() << std::endl;
    if(phaseOrd == keys.run) {
        std::thread asyncThread{threadFn};
        asyncThread.detach();
    }
    return true;
}

void threadFn() {
    std::cout << "[sample-mqtt-user] Started thread" << std::endl;
    auto threadScope = ggapi::ThreadScope::claimThread();

    while(true) {
        auto request{threadScope.createStruct()};
        request.put(keys.topicName, "hello");
        request.put(keys.qos, 1);
        request.put(keys.payload, "Hello world!");

        std::cout << "[sample-mqtt-user] Sending..." << std::endl;
        ggapi::Struct result = threadScope.sendToTopic(keys.publishToIoTCoreTopic, request);
        std::cout << "[sample-mqtt-user] Sending complete." << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}
