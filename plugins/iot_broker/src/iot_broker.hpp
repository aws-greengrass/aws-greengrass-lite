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
    [[nodiscard]] const char *what() const noexcept override {
        return "MQTT Failed setup MQTT client builder";
    }
};

class mqttClientException : public ggapi::GgApiError {
    [[nodiscard]] const char *what() const noexcept override {
        return "MQTT failed to initialize the client";
    }
};

class mqttClienFailedToStart : public ggapi::GgApiError {
    [[nodiscard]] const char *what() const noexcept override {
        return "MQTT client failed to start";
    }
};

class IotBroker : public ggapi::Plugin {
    struct Keys {
        ggapi::Symbol publishToIoTCoreTopic{"aws.greengrass.PublishToIoTCore"};
        ggapi::Symbol ipcPublishToIoTCoreTopic{"IPC::aws.greengrass#PublishToIoTCore"};
        ggapi::Symbol subscribeToIoTCoreTopic{"aws.greengrass.SubscribeToIoTCore"};
        ggapi::Symbol requestDeviceProvisionTopic{"aws.greengrass.RequestDeviceProvision"};
        ggapi::Symbol topicName{"topicName"};
        ggapi::Symbol topicFilter{"topicFilter"};
        ggapi::Symbol qos{"qos"};
        ggapi::Symbol payload{"payload"};
        ggapi::Symbol lpcResponseTopic{"lpcResponseTopic"};
        ggapi::Symbol message{"message"};
        ggapi::Symbol shape{"shape"};
        ggapi::Symbol errorCode{"errorCode"};
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

    static std::string base64Decode(std::string encoded) {
        // TODO: perform this conversion in-place
        Aws::Crt::String copy(encoded.size(), '\0');
        std::copy(encoded.begin(), encoded.end(), copy.begin());
        auto v = Aws::Crt::Base64Decode(copy);

        auto &decoded = encoded;
        decoded.resize(v.size(), '\0');
        std::copy(v.begin(), v.end(), decoded.begin());
        return decoded;
    }

    static std::string base64Encode(std::string decoded) {
        // TODO: perform this conversion in-place
        Aws::Crt::Vector<uint8_t> copy(decoded.size(), u'\0');
        std::copy(decoded.begin(), decoded.end(), copy.begin());
        auto str = Aws::Crt::Base64Encode(copy);

        auto &encoded = decoded;
        decoded.resize(str.size(), '\0');
        std::copy(str.begin(), str.end(), decoded.begin());
        return decoded;
    }

    void initMqtt();

    std::unordered_multimap<TopicFilter, ggapi::Symbol, TopicFilter::Hash> _subscriptions;
    std::shared_mutex _subscriptionMutex;
    std::shared_ptr<Aws::Crt::Mqtt5::Mqtt5Client> _client;
};
