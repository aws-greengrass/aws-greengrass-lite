#pragma once

#include <aws/crt/Api.h>
#include <aws/crt/Types.h>
#include <aws/crt/mqtt/Mqtt5Packets.h>
#include <aws/iot/Mqtt5Client.h>
#include <cpp_api.hpp>
#include <logging.hpp>
#include <memory>
#include <mqtt/topic_filter.hpp>
#include <plugin.hpp>
#include <shared_mutex>

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
    struct Keys {
        ggapi::Symbol publishToIoTCoreTopic{"aws.greengrass.PublishToIoTCore"};
        ggapi::Symbol ipcPublishToIoTCoreTopic{"IPC::aws.greengrass#PublishToIoTCore"};
        ggapi::Symbol subscribeToIoTCoreTopic{"aws.greengrass.SubscribeToIoTCore"};
        ggapi::Symbol ipcSubscribeToIoTCoreTopic{"IPC::aws.greengrass#SubscribeToIoTCore"};
        ggapi::Symbol requestDeviceProvisionTopic{"aws.greengrass.RequestDeviceProvision"};
        ggapi::Symbol topicName{"topicName"};
        ggapi::Symbol qos{"qos"};
        ggapi::Symbol payload{"payload"};
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

    // TES
    std::string _iotRoleAlias;
    std::string _savedToken;

public:
    bool onInitialize(ggapi::Struct data) override;
    bool onStart(ggapi::Struct data) override;
    bool onStop(ggapi::Struct data) override;
    bool onError_stop(ggapi::Struct data) override;

    static IotBroker &get() {
        static IotBroker instance{};
        return instance;
    }

    // TES
    bool tesOnStart(ggapi::Struct data);
    bool tesOnRun();
    void tesRefresh();
    ggapi::Struct retrieveToken(ggapi::Task, ggapi::Symbol, ggapi::Struct callData);

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
