#include "aws/crt/Types.h"
#include "aws/crt/mqtt/Mqtt5Types.h"
#include <aws/crt/Api.h>
#include <aws/crt/UUID.h>
#include <aws/crt/mqtt/Mqtt5Packets.h>
#include <aws/iot/Mqtt5Client.h>
#include <cassert>
#include <cpp_api.hpp>
#include <iostream>
#include <memory>
#include <regex>
#include <thread>
#include <unordered_map>

struct Keys {
    ggapi::StringOrd start{"start"};
    ggapi::StringOrd run{"run"};
    ggapi::StringOrd publishToIoTCoreTopic{"aws.greengrass.PublishToIoTCore"};
    ggapi::StringOrd subscribeToIoTCoreTopic{"aws.greengrass.SubscribeToIoTCore"};
    ggapi::StringOrd topicName{"topicName"};
    ggapi::StringOrd topicFilter{"topicFilter"};
    ggapi::StringOrd qos{"qos"};
    ggapi::StringOrd payload{"payload"};
    ggapi::StringOrd lpcResponseTopic{"lpcResponseTopic"};
};

static const Keys keys;

static int demo();
static bool startPhase(std::string, std::string, std::string);

static std::unordered_multimap<std::string, ggapi::StringOrd> subscriptions;
static std::mutex subscriptionMutex;

// Initializes global CRT API
// TODO: What happens when multiple plugins use the CRT?
static const Aws::Crt::ApiHandle apiHandle;

static std::shared_ptr<Aws::Crt::Mqtt5::Mqtt5Client> client;

ggapi::Struct publishHandler(ggapi::Scope task, ggapi::StringOrd, ggapi::Struct args) {
    std::string topic{args.get<std::string>(keys.topicName)};
    int qos{args.get<int>(keys.qos)};
    std::string payload{args.get<std::string>(keys.payload)};

    std::cerr << "[mqtt-plugin] Sending " << payload << " to " << topic << std::endl;

    auto onPublishComplete = [](int,
                                const std::shared_ptr<Aws::Crt::Mqtt5::PublishResult> &result) {
        if(!result->wasSuccessful()) {
            std::cerr << "[mqtt-plugin] Publish failed with error_code: " << result->getErrorCode()
                      << std::endl;
            return;
        }

        auto puback = std::dynamic_pointer_cast<Aws::Crt::Mqtt5::PubAckPacket>(result->getAck());

        if(puback) {

            if(puback->getReasonCode() == 0) {
                std::cerr << "[mqtt-plugin] Puback success" << std::endl;
            } else {
                std::cerr << "[mqtt-plugin] Puback failed: " << puback->getReasonString().value()
                          << std::endl;
            }
        }
    };

    auto publish = std::make_shared<Aws::Crt::Mqtt5::PublishPacket>(
        Aws::Crt::String(topic),
        ByteCursorFromString(Aws::Crt::String(payload)),
        static_cast<Aws::Crt::Mqtt5::QOS>(qos)
    );

    if(!client->Publish(publish, onPublishComplete)) {
        std::cerr << "[mqtt-plugin] Publish failed" << std::endl;
    }

    return task.createStruct();
}

ggapi::Struct subscribeHandler(ggapi::Scope task, ggapi::StringOrd, ggapi::Struct args) {
    std::string topicFilter{args.get<std::string>(keys.topicFilter)};
    int qos{args.get<int>(keys.qos)};
    ggapi::StringOrd responseTopic{args.get<uint32_t>(keys.lpcResponseTopic)};

    std::cerr << "[mqtt-plugin] Subscribing to " << topicFilter << std::endl;

    auto onSubscribeComplete = [topicFilter, responseTopic](
                                   int error_code,
                                   const std::shared_ptr<Aws::Crt::Mqtt5::SubAckPacket> &suback
                               ) {
        if(error_code != 0) {
            std::cerr << "[mqtt-plugin] Subscribe failed with error_code: " << error_code
                      << std::endl;
            return;
        }

        if(suback && !suback->getReasonCodes().empty()) {
            auto reasonCode = suback->getReasonCodes()[0];
            if(reasonCode >= Aws::Crt::Mqtt5::SubAckReasonCode::AWS_MQTT5_SARC_UNSPECIFIED_ERROR) {
                std::cerr << "[mqtt-plugin] Subscribe rejected with reason code: " << reasonCode
                          << std::endl;
                return;
            } else {
                std::cerr << "[mqtt-plugin] Subscribe accepted" << std::endl;
            }
        };

        {
            std::lock_guard<std::mutex> lock(subscriptionMutex);
            subscriptions.insert({topicFilter, responseTopic});
        }
    };

    auto subscribe = std::make_shared<Aws::Crt::Mqtt5::SubscribePacket>();
    subscribe->WithSubscription(std::move(Aws::Crt::Mqtt5::Subscription(
        Aws::Crt::String(topicFilter), static_cast<Aws::Crt::Mqtt5::QOS>(qos)
    )));

    if(!client->Subscribe(subscribe, onSubscribeComplete)) {
        std::cerr << "[mqtt-plugin] Subscribe failed" << std::endl;
    }

    return task.createStruct();
}

extern "C" bool greengrass_lifecycle(
    uint32_t moduleHandle, uint32_t phase, uint32_t dataHandle
) noexcept {
    ggapi::StringOrd phaseOrd{phase};
    ggapi::Struct structData{dataHandle};

    ggapi::Struct configStruct = structData.getValue<ggapi::Struct>({"config"});

    std::string certPath = configStruct.getValue<std::string>({"system", "certificateFilePath"});
    std::string keyPath = configStruct.getValue<std::string>({"system", "privateKeyPath"});
    std::string credEndpoint = configStruct.getValue<std::string>(
        {"services", "aws.greengrass.Nucleus-Lite", "configuration", "iotCredEndpoint"}
    );

    std::cerr << "[mqtt-plugin] Running lifecycle phase " << phaseOrd.toString() << std::endl;

    if(phaseOrd == keys.start) {
        return startPhase(credEndpoint, certPath, keyPath);
    }
    return true;
}

static std::ostream &operator<<(std::ostream &os, Aws::Crt::ByteCursor bc) {
    for(int byte : std::basic_string_view<uint8_t>(bc.ptr, bc.len)) {
        if(isprint(byte)) {
            os << static_cast<char>(byte);
        } else {
            os << '\\' << byte;
        }
    }
    return os;
}

static bool startPhase(
    std::string credEndpoint, std::string certificateFilePath, std::string privateKeyPath
) {
    std::promise<bool> connectionPromise;

    {
        Aws::Crt::String crtEndpoint{credEndpoint};
        std::unique_ptr<Aws::Iot::Mqtt5ClientBuilder> builder{
            Aws::Iot::Mqtt5ClientBuilder::NewMqtt5ClientBuilderWithMtlsFromPath(
                crtEndpoint, certificateFilePath.c_str(), privateKeyPath.c_str()
            )};

        if(!builder) {
            std::cerr << "[mqtt-plugin] Failed to set up MQTT client builder." << std::endl;
            return false;
        }

        auto connectOptions = std::make_shared<Aws::Crt::Mqtt5::ConnectPacket>();
        connectOptions->WithClientId("gglite-test");
        builder->WithConnectOptions(connectOptions);

        builder->WithClientConnectionSuccessCallback(
            [&connectionPromise](const Aws::Crt::Mqtt5::OnConnectionSuccessEventData &eventData) {
                std::cerr << "[mqtt-plugin] Connection successful with clientid "
                          << eventData.negotiatedSettings->getClientId() << "." << std::endl;
                connectionPromise.set_value(true);
            }
        );

        builder->WithClientConnectionFailureCallback(
            [&connectionPromise](const Aws::Crt::Mqtt5::OnConnectionFailureEventData &eventData) {
                std::cerr << "[mqtt-plugin] Connection failed: "
                          << aws_error_debug_str(eventData.errorCode) << "." << std::endl;
                connectionPromise.set_value(false);
            }
        );

        builder->WithPublishReceivedCallback(
            [](const Aws::Crt::Mqtt5::PublishReceivedEventData &eventData) {
                if(!eventData.publishPacket) {
                    return;
                }

                std::string topic{eventData.publishPacket->getTopic()};
                std::string payload{
                    reinterpret_cast<char *>(eventData.publishPacket->getPayload().ptr),
                    eventData.publishPacket->getPayload().len};

                std::cerr << "[mqtt-plugin] Publish recieved on topic " << topic << ": " << payload
                          << std::endl;

                static thread_local auto threadScope = ggapi::ThreadScope::claimThread();
                auto response{threadScope.createStruct()};
                response.put(keys.topicName, topic);
                response.put(keys.payload, payload);

                {
                    std::lock_guard<std::mutex> lock(subscriptionMutex);
                    for(const auto &[key, value] : subscriptions) {

                        // TODO: Do this in a better way
                        std::string rx = key;
                        rx = std::regex_replace(rx, std::regex("\\\\", std::regex::basic), "\\\\");
                        rx = std::regex_replace(rx, std::regex("\\.", std::regex::basic), "\\.");
                        rx = std::regex_replace(rx, std::regex("\\[", std::regex::basic), "\\[");
                        rx = std::regex_replace(rx, std::regex("\\*", std::regex::basic), "\\*");
                        rx = std::regex_replace(rx, std::regex("\\^", std::regex::basic), "\\^");
                        rx = std::regex_replace(rx, std::regex("\\$", std::regex::basic), "\\$");
                        rx = std::regex_replace(rx, std::regex("+", std::regex::basic), "[^/]*");
                        rx = std::regex_replace(rx, std::regex("#", std::regex::basic), ".*");

                        if(std::regex_match(topic, std::regex(rx, std::regex::basic))) {
                            (void) threadScope.sendToTopic(value, response);
                        }
                    }
                }
            }
        );

        client = builder->Build();
    }

    if(!client) {
        std::cerr << "[mqtt-plugin] Failed to init MQTT client: "
                  << Aws::Crt::ErrorDebugString(Aws::Crt::LastError()) << "." << std::endl;
        return false;
    }

    if(!client->Start()) {
        std::cerr << "[mqtt-plugin] Failed to start MQTT client." << std::endl;
        return false;
    }

    if(!connectionPromise.get_future().get()) {
        return false;
    }

    (void) ggapi::Scope::thisTask().subscribeToTopic(keys.publishToIoTCoreTopic, publishHandler);
    (void) ggapi::Scope::thisTask().subscribeToTopic(
        keys.subscribeToIoTCoreTopic, subscribeHandler
    );

    return true;
}
