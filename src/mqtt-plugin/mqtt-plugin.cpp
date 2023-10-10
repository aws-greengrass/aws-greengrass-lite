#include "aws/crt/Types.h"
#include "aws/crt/mqtt/Mqtt5Types.h"
#include <aws/crt/Api.h>
#include <aws/crt/UUID.h>
#include <aws/crt/mqtt/Mqtt5Packets.h>
#include <aws/iot/Mqtt5Client.h>
#include <cassert>
#include <cpp_api.h>
#include <iostream>
#include <memory>
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

static int demo();
static bool startPhase();

// Initializes global CRT API
// TODO: What happens when multiple plugins use the CRT?
static const Aws::Crt::ApiHandle apiHandle;

static std::shared_ptr<Aws::Crt::Mqtt5::Mqtt5Client> client;

ggapi::Struct publishHandler(ggapi::Scope task, ggapi::StringOrd, ggapi::Struct args) {
    std::string topic{args.get<std::string>(keys.topicName)};
    int qos{args.get<int>(keys.qos)};
    std::string payload{args.get<std::string>(keys.payload)};

    std::cout << "[mqtt-plugin] Sending " << payload << " to " << topic << std::endl;

    auto onPublishComplete = [](int,
                                const std::shared_ptr<Aws::Crt::Mqtt5::PublishResult> &result) {
        if(!result->wasSuccessful()) {
            std::cout << "[mqtt-plugin] Publish failed with error_code: " << result->getErrorCode()
                      << std::endl;
            return;
        }

        auto puback = std::dynamic_pointer_cast<Aws::Crt::Mqtt5::PubAckPacket>(result->getAck());

        if(puback) {

            if(puback->getReasonCode() == 0) {
                std::cout << "[mqtt-plugin] Puback success." << std::endl;
            } else {
                std::cout << "[mqtt-plugin] Puback failed: " << puback->getReasonString().value()
                          << "." << std::endl;
            }
        }
    };

    auto publish = std::make_shared<Aws::Crt::Mqtt5::PublishPacket>(
        Aws::Crt::String(topic),
        ByteCursorFromString(Aws::Crt::String(payload)),
        static_cast<Aws::Crt::Mqtt5::QOS>(qos)
    );

    if(!client->Publish(publish, onPublishComplete)) {
        std::cout << "[mqtt-plugin] Publish failed." << std::endl;
    }

    return task.createStruct();
}

extern "C" bool greengrass_lifecycle(
    uint32_t moduleHandle, uint32_t phase, uint32_t data
) noexcept {
    ggapi::StringOrd phaseOrd{phase};

    std::cout << "[mqtt-plugin] Running lifecycle phase " << phaseOrd.toString() << std::endl;

    if(phaseOrd == keys.start) {
        return startPhase();
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

static bool startPhase() {
    std::promise<bool> connectionPromise;

    {
        // TODO: Use config for address and cert
        std::unique_ptr<Aws::Iot::Mqtt5ClientBuilder> builder{
            Aws::Iot::Mqtt5ClientBuilder::NewMqtt5ClientBuilderWithMtlsFromPath(
                "<insert-id>-ats.iot.us-west-2.amazonaws.com", "device.pem", "device.key"
            )};

        if(!builder) {
            std::cout << "[mqtt-plugin] Failed to set up MQTT client builder." << std::endl;
            return false;
        }

        auto connectOptions = std::make_shared<Aws::Crt::Mqtt5::ConnectPacket>();
        connectOptions->WithClientId("gglite-test");
        builder->WithConnectOptions(connectOptions);

        builder->WithClientConnectionSuccessCallback(
            [&connectionPromise](const Aws::Crt::Mqtt5::OnConnectionSuccessEventData &eventData) {
                std::cout << "[mqtt-plugin] Connection successful with clientid "
                          << eventData.negotiatedSettings->getClientId() << "." << std::endl;
                connectionPromise.set_value(true);
            }
        );

        builder->WithClientConnectionFailureCallback(
            [&connectionPromise](const Aws::Crt::Mqtt5::OnConnectionFailureEventData &eventData) {
                std::cout << "[mqtt-plugin] Connection failed: "
                          << aws_error_debug_str(eventData.errorCode) << "." << std::endl;
                connectionPromise.set_value(false);
            }
        );

        builder->WithPublishReceivedCallback(
            [](const Aws::Crt::Mqtt5::PublishReceivedEventData &eventData) {
                if(!eventData.publishPacket) {
                    return;
                }

                std::cout << "[mqtt-plugin] Publish recieved on topic "
                          << eventData.publishPacket->getTopic() << ": "
                          << eventData.publishPacket->getPayload() << std::endl;
            }
        );

        client = builder->Build();
    }

    if(!client) {
        std::cout << "[mqtt-plugin] Failed to init MQTT client: "
                  << Aws::Crt::ErrorDebugString(Aws::Crt::LastError()) << "." << std::endl;
        return false;
    }

    if(!client->Start()) {
        std::cout << "[mqtt-plugin] Failed to start MQTT client." << std::endl;
        return false;
    }

    if(!connectionPromise.get_future().get()) {
        return false;
    }

    (void) ggapi::Scope::thisTask().subscribeToTopic(keys.publishToIoTCoreTopic, publishHandler);

    return true;
}
