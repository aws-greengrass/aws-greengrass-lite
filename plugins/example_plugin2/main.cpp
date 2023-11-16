#include <iostream>
#include <plugin.hpp>
#include <thread>

struct Keys {
    const ggapi::StringOrd publishToIoTCoreTopic{"aws.greengrass."
                                                 "PublishToIoTCore"};
    const ggapi::StringOrd topicName{"topicName"};
    const ggapi::StringOrd qos{"qos"};
    const ggapi::StringOrd payload{"payload"};
    const ggapi::StringOrd retain{"retain"};
    const ggapi::StringOrd userProperties{"userProperties"};
    const ggapi::StringOrd messageExpiryIntervalSeconds{"messageExpiryIntervalSeconds"};
    const ggapi::StringOrd correlationData{"correlationData"};
    const ggapi::StringOrd responseTopic{"responseTopic"};
    const ggapi::StringOrd payloadFormat{"payloadFormat"};
    const ggapi::StringOrd contentType{"contentType"};
};

class ExamplePlugin : public ggapi::Plugin {
    static const Keys KEYS;
    std::thread _asyncThread;

public:
    void beforeLifecycle(ggapi::StringOrd phase, ggapi::Struct data) override;
    bool onStart(ggapi::Struct data) override;

    bool onRun(ggapi::Struct data) override;

    static ggapi::Struct publishToIoTCoreResponder(
        ggapi::Task task, ggapi::StringOrd topic, ggapi::Struct respData);
    static ggapi::Struct publishToIoTCoreListener(
        ggapi::Task task, ggapi::StringOrd topic, ggapi::Struct callData);

    static ExamplePlugin &get() {
        static ExamplePlugin instance{};
        return instance;
    }

    void asyncThreadFn();
};

const Keys ExamplePlugin::KEYS{};

extern "C" EXPORT bool greengrass_lifecycle(
    uint32_t moduleHandle, uint32_t phase, uint32_t data) noexcept {
    return ExamplePlugin::get().lifecycle(moduleHandle, phase, data);
}

void ExamplePlugin::beforeLifecycle(ggapi::StringOrd phase, ggapi::Struct data) {
    std::cout << "Running lifecycle plugins 2... " << ggapi::StringOrd{phase}.toString()
              << std::endl;
}

bool ExamplePlugin::onStart(ggapi::Struct data) {
    return true;
}

bool ExamplePlugin::onRun(ggapi::Struct data) {
    _asyncThread = std::thread{&ExamplePlugin::asyncThreadFn, this};
    return true;
}

ggapi::Struct ExamplePlugin::publishToIoTCoreListener(
    ggapi::Task task, ggapi::StringOrd topic, ggapi::Struct callData) {
    std::ignore = task; // task handle for task operations
    std::ignore = topic; // topic name in case same callback used for multiple topics
    // real work
    std::string destTopic{callData.get<std::string>(KEYS.topicName)};
    int qos{callData.get<int>(KEYS.qos)};
    ggapi::Struct payload{callData.get<ggapi::Struct>(KEYS.payload)};
    // ...
    // construct response
    auto response = ggapi::Struct::create();
    response.put("status", 1U);
    // Variables retrieved for example usage and debugging, they're not actually used
    std::ignore = qos;
    std::ignore = payload;
    std::ignore = destTopic;
    // return response
    return response;
}

ggapi::Struct ExamplePlugin::publishToIoTCoreResponder(
    ggapi::Task task, ggapi::StringOrd topic, ggapi::Struct respData) {
    std::ignore = task; // task handle for task operations
    std::ignore = topic; // topic name in case same callback used for multiple topics
    if(!respData) {
        // unhandled
        return respData;
    }
    uint32_t status{respData.get<uint32_t>("status")};
    std::ignore = status; // variable retrieved for example usage and debugging
    return respData;
}

// NOLINTNEXTLINE(*-convert-member-functions-to-static)
void ExamplePlugin::asyncThreadFn() {
    std::cout << "Running async plugins 2..." << std::endl;

    ggapi::Subscription publishToIoTCoreListenerHandle{
        getScope().subscribeToTopic(KEYS.publishToIoTCoreTopic, publishToIoTCoreListener)};

    auto request{ggapi::Struct::create()};
    request.put(KEYS.topicName, "some-cloud-topic")
        .put(KEYS.qos, "1") // string gets converted to int later
        .put(KEYS.payload, ggapi::Struct::create().put("Foo", 1U));

    // Async style
    auto newTask = ggapi::Task::sendToTopicAsync(
        KEYS.publishToIoTCoreTopic, request, publishToIoTCoreResponder, -1);
    auto respData = newTask.waitForTaskCompleted();
    uint32_t status{respData.get<uint32_t>("status")};

    // Sync style
    auto syncRespData = ggapi::Task::sendToTopic(KEYS.publishToIoTCoreTopic, request);
    uint32_t syncStatus{syncRespData.get<uint32_t>("status")};

    std::cout << "Ping..." << std::endl;

    auto pingData = ggapi::Struct::create().put("ping", "abcde");
    auto pongData = ggapi::Task::sendToTopic(ggapi::StringOrd{"test"}, pingData);
    auto pongString = pongData.get<std::string>("pong");

    std::cout << "Pong..." << pongString << std::endl;

    // Variables retrieved for debugging and example usage
    std::ignore = publishToIoTCoreListenerHandle;
    std::ignore = status;
    std::ignore = syncStatus;
}
