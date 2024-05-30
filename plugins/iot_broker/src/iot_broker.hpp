#pragma once

#include "thread_safe_queue.hpp"
#include <cpp_api.hpp>
#include <logging.hpp>
#include <memory>
#include <mqtt/topic_filter.hpp>
#include <plugin.hpp>
#include <shared_device_sdk.hpp>
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
        ggapi::Symbol subscribeConnTopic{"aws.greengrass.SubscribeConnStatus"};
        ggapi::Symbol topicName{"topicName"};
        ggapi::Symbol qos{"qos"};
        ggapi::Symbol payload{"payload"};
        ggapi::Symbol message{"message"};
        ggapi::Symbol shape{"shape"};
        ggapi::Symbol channel{"channel"};
        ggapi::Symbol serviceModelType{"serviceModelType"};
        ggapi::Symbol terminate{"terminate"};
        ggapi::Symbol status{"status"};
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

    mutable std::shared_mutex _mutex;
    ggapi::Struct _nucleus;
    ggapi::Struct _system;

    // Subscriptions
    ggapi::Subscription _publishSubs;
    ggapi::Subscription _ipcPublishSubs;
    ggapi::Subscription _subscribeSubs;
    ggapi::Subscription _ipcSubscribeSubs;
    ggapi::Subscription _requestTesSubs;
    ggapi::Subscription _connStatusSubs;

    // TES
    std::string _iotRoleAlias;
    std::string _savedToken;

public:
    void onInitialize(ggapi::Struct data) override;
    void onStart(ggapi::Struct data) override;
    void onStop(ggapi::Struct data) override;

    static IotBroker &get() {
        static IotBroker instance{};
        return instance;
    }

    // TES
    bool tesOnStart(const ggapi::Struct &data);
    void tesRefresh();
    ggapi::Promise retrieveToken(ggapi::Symbol, const ggapi::Container &callData);
    void retrieveTokenAsync(const ggapi::Struct &callData, ggapi::Promise promise);

    int JobsRun();

private:
    //Jobs
    std::atomic<bool> _done;
    //Jobs End

    static const Keys keys;
    ggapi::Promise publishHandler(ggapi::Symbol, const ggapi::Container &args);
    ggapi::Promise ipcPublishHandler(ggapi::Symbol, const ggapi::Container &args);
    void publishHandlerAsync(const ggapi::Struct &args, ggapi::Promise promise);
    ggapi::Promise subscribeHandler(ggapi::Symbol, const ggapi::Container &args);
    ggapi::Promise ipcSubscribeHandler(ggapi::Symbol, const ggapi::Container &args);
    void subscribeHandlerAsync(const ggapi::Struct &args, ggapi::Promise promise);

    void initMqtt();
    void connectionThread(ggapi::Struct data);
    std::thread _conn;
    std::thread _worker;

    ThreadSafeQueue<ggapi::Struct> _queue;
    void queueWorker();

    using Key = TopicFilter<Aws::Crt::StlAllocator<char>>;
    std::vector<std::tuple<Key, ggapi::Channel>> _subscriptions;
    std::shared_mutex _subscriptionMutex; // TODO: fold this with _mutex?
    std::shared_ptr<Aws::Crt::Mqtt5::Mqtt5Client> _client;

    std::vector<ggapi::Channel> _connStatusListeners;
    std::shared_mutex _connStatusMutex;
    bool _connected = false;
    void updateConnStatus(bool connected);
    ggapi::Promise connStatusHandler(ggapi::Symbol, const ggapi::Container &args);
};
