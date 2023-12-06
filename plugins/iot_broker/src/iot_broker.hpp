#pragma once

#include <iostream>
#include <memory>
#include <shared_mutex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <unordered_map>

#include <aws/crt/Api.h>
#include <aws/crt/Types.h>
#include <aws/crt/mqtt/Mqtt5Packets.h>
#include <aws/iot/Mqtt5Client.h>

#include "topic_filter.hpp"
#include <plugin.hpp>

class mqttBuilderException : public ggapi::GgApiError {
    virtual const char *what() const noexcept {
        return "MQTT Failed setup MQTT client builder";
    }
};

class mqttClientException : public ggapi::GgApiError {
    virtual const char *what() const noexcept {
        return "MQTT failed to initialize the client";
    }
};

class mqttClienFailedToStart : public ggapi::GgApiError {
    virtual const char *what() const noexcept {
        return "MQTT client failed to start";
    }
};

class IotBroker : public ggapi::Plugin {
    struct Keys {
        ggapi::Symbol publishToIoTCoreTopic{"aws.greengrass.PublishToIoTCore"};
        ggapi::Symbol subscribeToIoTCoreTopic{"aws.greengrass.SubscribeToIoTCore"};
        ggapi::Symbol requestDeviceProvisionTopic{"aws.greengrass.RequestDeviceProvision"};
        ggapi::Symbol topicName{"topicName"};
        ggapi::Symbol topicFilter{"topicFilter"};
        ggapi::Symbol qos{"qos"};
        ggapi::Symbol payload{"payload"};
        ggapi::Symbol lpcResponseTopic{"lpcResponseTopic"};
    };

    struct ThingInfo {
        std::string thingName;
        std::string credEndpoint;
        std::string dataEndpoint;
        std::string certPath;
        std::string keyPath;
        std::string rootCaPath;
        std::string rootPath;
    };

    struct ThingInfo _thingInfo;
    std::thread _asyncThread;
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
    ggapi::Struct subscribeHandler(ggapi::Task, ggapi::Symbol, ggapi::Struct args);

    ggapi::Struct publishHandlerImpl(ggapi::Struct args);
    ggapi::Struct subscribeHandlerImpl(ggapi::Struct args);

    void initMqtt();

    std::unordered_multimap<TopicFilter, ggapi::Symbol, TopicFilter::Hash> _subscriptions;
    std::shared_mutex _subscriptionMutex;
    std::shared_ptr<Aws::Crt::Mqtt5::Mqtt5Client> _client;
};
