#include <cpp_api.hpp>
#include <iostream>
#include <thread>

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
};

const Keys keys;
std::thread asyncThread;

void asyncThreadFn();

extern "C" [[maybe_unused]] EXPORT bool greengrass_lifecycle(
    uint32_t moduleHandle, uint32_t phase, uint32_t data
) {
    std::cout << "Running lifecycle plugins 2... " << ggapi::StringOrd{phase}.toString()
              << std::endl;
    ggapi::StringOrd phaseOrd{phase};
    if(phaseOrd == keys.run) {
        asyncThread = std::thread{asyncThreadFn};
        asyncThread.detach();
    }
    return true;
}

ggapi::Struct publishToIoTCoreListener(
    ggapi::Scope task, ggapi::StringOrd topic, ggapi::Struct callData
) {
    // real work
    std::string destTopic{callData.get<std::string>(keys.topicName)};
    int qos{callData.get<int>(keys.qos)};
    ggapi::Struct payload{callData.get<ggapi::Struct>(keys.payload)};
    // ...
    // construct response
    ggapi::Struct response = task.createStruct();
    response.put("status", 1U);
    // return response
    return response;
}

ggapi::Struct publishToIoTCoreResponder(
    ggapi::Scope task, ggapi::StringOrd topic, ggapi::Struct respData
) {
    if(!respData) {
        // unhandled
        return respData;
    }
    uint32_t status{respData.get<uint32_t>("status")};
    return respData;
}

void asyncThreadFn() {
    std::cout << "Running async plugins 2..." << std::endl;
    auto threadScope = ggapi::ThreadScope::claimThread(); // assume long-running
                                                          // thread, this
                                                          // provides a
                                                          // long-running task
                                                          // handle

    ggapi::ObjHandle publishToIoTCoreListenerHandle{
        threadScope.subscribeToTopic(keys.publishToIoTCoreTopic, publishToIoTCoreListener)};

    auto request{threadScope.createStruct()};
    request.put(keys.topicName, "some-cloud-topic")
        .put(keys.qos, "1") // string gets converted to int later
        .put(keys.payload, threadScope.createStruct().put("Foo", 1U));

    // Async style
    ggapi::Scope newTask = threadScope.sendToTopicAsync(
        keys.publishToIoTCoreTopic, request, publishToIoTCoreResponder, -1
    );
    ggapi::Struct respData = newTask.waitForTaskCompleted();
    uint32_t status{respData.get<uint32_t>("status")};

    // Sync style
    ggapi::Struct syncRespData = threadScope.sendToTopic(keys.publishToIoTCoreTopic, request);
    uint32_t syncStatus{syncRespData.get<uint32_t>("status")};

    std::cout << "Ping..." << std::endl;

    ggapi::Struct pingData = threadScope.createStruct().put("ping", "abcde");
    ggapi::Struct pongData = threadScope.sendToTopic(
        ggapi::StringOrd{"tes"
                         "t"},
        pingData
    );
    auto pongString = pongData.get<std::string>("pong");

    std::cout << "Pong..." << pongString << std::endl;

    ggapi::Buffer buf = threadScope.createBuffer();
    ggapi::Buffer::Vector vecData;
    vecData.push_back(1);
    vecData.push_back(2);
    vecData.push_back(3);
    buf.insert(0, vecData);
    buf.insert(1, vecData);
    vecData.resize(10);
    buf.get(0, vecData);
}
