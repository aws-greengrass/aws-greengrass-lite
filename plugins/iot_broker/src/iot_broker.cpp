//
// Created by Julicher, Joe on 12/5/23.
//

#include "iot_broker.h"

ggapi::Struct IotBroker::publishHandler(ggapi::Task, ggapi::Symbol, ggapi::Struct args) {
    std::cerr << "[mqtt-plugin] received a publish request" << std::endl;
    return get().publishHandlerImpl(args);
}

ggapi::Struct IotBroker::publishHandlerImpl(ggapi::Struct args) {
    auto topic{args.get<std::string>(keys.topicName)};
    auto qos{args.get<int>(keys.qos)};
    auto payload{args.get<std::string>(keys.payload)};

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
        static_cast<Aws::Crt::Mqtt5::QOS>(qos));

    if(!_client->Publish(publish, onPublishComplete)) {
        std::cerr << "[mqtt-plugin] Publish failed" << std::endl;
    }

    return ggapi::Struct::create();
}

ggapi::Struct IotBroker::subscribeHandler(ggapi::Task, ggapi::Symbol, ggapi::Struct args) {
    return get().subscribeHandlerImpl(args);
}

ggapi::Struct IotBroker::subscribeHandlerImpl(ggapi::Struct args) {
    TopicFilter topicFilter{args.get<std::string>(keys.topicFilter)};
    int qos{args.get<int>(keys.qos)};
    ggapi::Symbol responseTopic{args.get<std::string>(keys.lpcResponseTopic)};

    std::cerr << "[mqtt-plugin] Subscribing to " << topicFilter.get() << std::endl;

    auto onSubscribeComplete = [this, topicFilter, responseTopic](
                                   int error_code,
                                   const std::shared_ptr<Aws::Crt::Mqtt5::SubAckPacket> &suback) {
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
        }

        {
            std::unique_lock lock(_subscriptionMutex);
            _subscriptions.insert({topicFilter, responseTopic});
        }
    };

    auto subscribe = std::make_shared<Aws::Crt::Mqtt5::SubscribePacket>();
    subscribe->WithSubscription(std::move(Aws::Crt::Mqtt5::Subscription(
        Aws::Crt::String(topicFilter.get()), static_cast<Aws::Crt::Mqtt5::QOS>(qos))));

    if(!_client->Subscribe(subscribe, onSubscribeComplete)) {
        std::cerr << "[mqtt-plugin] Subscribe failed" << std::endl;
    }

    return ggapi::Struct::create();
}


const Keys IotBroker::keys{};
