#pragma once

#include <aws/crt/Api.h>
#include <aws/crt/Types.h>
#include <aws/crt/mqtt/Mqtt5Packets.h>
#include <aws/iot/Mqtt5Client.h>
#include <cpp_api.hpp>
#include <iostream>
#include <list>
#include <memory>
#include <mqtt/topic_filter.hpp>
#include <plugin.hpp>
#include <shared_mutex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <unordered_map>

class MqttBuilderException : public ggapi::GgApiError {
public:
    MqttBuilderException()
        : ggapi::GgApiError("MqttBuilderException", "MQTT Failed setup MQTT client builder") {
    }
};

class MqttClientException : public ggapi::GgApiError {
public:
    MqttClientException()
        : ggapi::GgApiError("MqttClientException", "MQTT failed to initialize the client") {
    }
};

class MqttClientFailedToStart : public ggapi::GgApiError {
public:
    MqttClientFailedToStart()
        : ggapi::GgApiError("MqttClientFailedToStart", "MQTT client failed to start") {
    }
};

using PacketHandler = std::function<ggapi::Struct(ggapi::Struct packet)>;

class IotBroker : public ggapi::Plugin {
public:
    // enum class PayloadFormat { BYTES, UTF8};
    //  Mapping of symbols to enums
    inline static const util::LookupTable PAYLOAD_FORMAT_MAP{
        0,
        aws_mqtt5_payload_format_indicator::AWS_MQTT5_PFI_BYTES,
        1,
        aws_mqtt5_payload_format_indicator::AWS_MQTT5_PFI_UTF8};

private:
    struct Keys {
        ggapi::Symbol publishToIoTCoreTopic{"aws.greengrass.PublishToIoTCore"};
        ggapi::Symbol ipcPublishToIoTCoreTopic{"IPC::aws.greengrass#PublishToIoTCore"};
        ggapi::Symbol subscribeToIoTCoreTopic{"aws.greengrass.SubscribeToIoTCore"};
        ggapi::Symbol ipcSubscribeToIoTCoreTopic{"IPC::aws.greengrass#SubscribeToIoTCore"};
        ggapi::Symbol requestDeviceProvisionTopic{"aws.greengrass.RequestDeviceProvision"};
        ggapi::Symbol topicName{"topicName"};
        ggapi::Symbol qos{"qos"};
        ggapi::Symbol payload{"payload"};
        ggapi::Symbol retain{"retain"};
        ggapi::Symbol userProperties{"userProperties"};
        ggapi::Symbol member{"member"};
        ggapi::Symbol key{"key"};
        ggapi::Symbol value{"value"};
        ggapi::Symbol messageExpiryIntervalSeconds{"messageExpiryIntervalSeconds"};
        ggapi::Symbol correlationData{"correlationData"};
        ggapi::Symbol responseTopic{"responseTopic"};
        ggapi::Symbol payloadFormat{"payloadFormat"};
        ggapi::Symbol message{"message"};
        ggapi::Symbol shape{"shape"};
        ggapi::Symbol errorCode{"errorCode"};
        ggapi::Symbol channel{"channel"};
        ggapi::Symbol serviceModelType{"serviceModelType"};
        ggapi::Symbol terminate{"terminate"};
    };

    struct ThingInfo {
        Aws::Crt::String thingName;
        std::string credEndpoint;
        Aws::Crt::String dataEndpoint;
        std::string certPath;
        std::string keyPath;
        std::string rootCaPath;
        std::string rootPath;
    } _thingInfo;

    std::atomic<ggapi::Struct> _nucleus;
    std::atomic<ggapi::Struct> _system;

public:
    bool onBootstrap(ggapi::Struct data) override;
    bool onBind(ggapi::Struct data) override;
    bool onDiscover(ggapi::Struct data) override;
    bool onStart(ggapi::Struct data) override;
    bool onRun(ggapi::Struct data) override;
    bool onTerminate(ggapi::Struct data) override;
    void beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) override;
    void afterLifecycle(ggapi::Symbol phase, ggapi::Struct data) override;

    static IotBroker &get() {
        static IotBroker instance{};
        return instance;
    }

private:
    static const Keys keys;
    ggapi::Struct publishHandler(ggapi::Task, ggapi::Symbol, ggapi::Struct args);
    ggapi::Struct ipcPublishHandler(ggapi::Task, ggapi::Symbol, ggapi::Struct args);
    ggapi::Struct subscribeHandler(ggapi::Task, ggapi::Symbol, ggapi::Struct args);

    ggapi::Struct ipcSubscribeHandler(ggapi::Task, ggapi::Symbol, ggapi::Struct args);
    std::variant<ggapi::Channel, uint32_t> commonSubscribeHandler(
        ggapi::Struct args, PacketHandler handler);

    void initMqtt();

    using Key = TopicFilter<Aws::Crt::StlAllocator<char>>;
    std::vector<std::tuple<Key, ggapi::Channel, PacketHandler>> _subscriptions;
    std::shared_mutex _subscriptionMutex;
    std::shared_ptr<Aws::Crt::Mqtt5::Mqtt5Client> _client;
};
