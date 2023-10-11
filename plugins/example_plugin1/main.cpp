#include <cpp_api.hpp>
#include <iostream>

struct Keys {
    ggapi::StringOrd start{"start"};
    ggapi::StringOrd run{"run"};
    ggapi::StringOrd publishToIoTCoreTopic{"aws.greengrass."
                                           "PublishToIoTCore"};
    ggapi::StringOrd topicName{"topicName"};
    ggapi::StringOrd qos{"qos"};
    ggapi::StringOrd payload{"payload"};
    ggapi::StringOrd retain{"retain"};
    ggapi::StringOrd userProperties{"userProperties"};
    ggapi::StringOrd messageExpiryIntervalSeconds{"messageExpiryIntervalSecon"
                                                  "ds"};
    ggapi::StringOrd correlationData{"correlationData"};
    ggapi::StringOrd responseTopic{"responseTopic"};
    ggapi::StringOrd payloadFormat{"payloadFormat"};
    ggapi::StringOrd contentType{"contentType"};

    static const Keys &get() {
        static std::unique_ptr<Keys> keyRef;
        if(keyRef == nullptr) {
            keyRef = std::make_unique<Keys>();
        }
        return *keyRef;
    }
};

ggapi::Struct testListener(ggapi::Scope task, ggapi::StringOrd topic, ggapi::Struct callData) {
    std::string pingMessage{callData.get<std::string>("ping")};
    ggapi::Struct response = task.createStruct();
    response.put("pong", pingMessage);
    return response;
}

void doStartPhase() {
    (void) ggapi::Scope::thisTask().subscribeToTopic(ggapi::StringOrd{"test"}, testListener);
}

void doRunPhase() {
}

extern "C" bool greengrass_lifecycle(uint32_t moduleHandle, uint32_t phase, uint32_t data) {
    std::cout << "Running lifecycle plugins 1... " << ggapi::StringOrd{phase}.toString()
              << std::endl;
    const auto &keys = Keys::get();

    ggapi::StringOrd phaseOrd{phase};
    if(phaseOrd == keys.start) {
        doStartPhase();
    } else if(phaseOrd == keys.run) {
        doRunPhase();
    }
    return true;
}
