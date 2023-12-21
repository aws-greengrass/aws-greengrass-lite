#include <cpp_api.hpp>
#include <iostream>
#include <mqtt/topic_filter.hpp>
#include <mqtt/topic_level_iterator.hpp>
#include <plugin.hpp>
#include <string>
#include <unordered_map>
#include <vector>

struct Keys {
    ggapi::StringOrd ipcPublishToTopic{"IPC::aws.greengrass#PublishToTopic"};
    ggapi::StringOrd ipcSubscribeToTopic{"IPC::aws.greengrass#SubscribeToTopic"};
    ggapi::StringOrd topic{"topic"};
    ggapi::StringOrd publishMessage{"publishMessage"};
    ggapi::StringOrd jsonMessage{"jsonMessage"};
    ggapi::StringOrd binaryMessage{"binaryMessage"};
    ggapi::StringOrd message{"message"};
    ggapi::StringOrd context{"context"};
    ggapi::StringOrd receiveMode{"receiveMode"};
    ggapi::StringOrd channel{"channel"};
    ggapi::StringOrd shape{"shape"};
    ggapi::StringOrd errorCode{"errorCode"};
    ggapi::StringOrd serviceModelType{"serviceModelType"};
    ggapi::StringOrd terminate{"terminate"};
};

static const Keys keys;

class LocalBroker : public ggapi::Plugin {
private:
    std::vector<std::pair<TopicFilter<>, ggapi::Channel>> _subscriptions;
    std::shared_mutex _subscriptionMutex;

public:
    void beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) override;
    bool onStart(ggapi::Struct data) override;

    ggapi::Struct publishToTopicHandler(
        ggapi::Task task, ggapi::Symbol topic, ggapi::Struct callData);

    ggapi::Struct subscribeToTopicHandler(
        ggapi::Task task, ggapi::Symbol topic, ggapi::Struct callData);

    static LocalBroker &get() {
        static LocalBroker instance{};
        return instance;
    }
};

ggapi::Struct LocalBroker::publishToTopicHandler(
    ggapi::Task, ggapi::Symbol, ggapi::Struct callData) {
    auto topic{callData.get<std::string>(keys.topic)};
    auto message{callData.get<ggapi::Struct>(keys.publishMessage)};

    auto context{ggapi::Struct::create().put(keys.topic, topic)};

    bool isBin = message.hasKey(keys.binaryMessage);
    bool isJson = message.hasKey(keys.jsonMessage);

    if((isBin && isJson) || (!isBin && !isJson)) {
        return ggapi::Struct::create().put(keys.errorCode, 1);
    }

    if(isBin) {
        auto binMsg{message.get<ggapi::Struct>(keys.binaryMessage)};
        binMsg.put(keys.context, context);
    } else {
        auto jsonMsg{message.get<ggapi::Struct>(keys.jsonMessage)};
        jsonMsg.put(keys.context, context);
    }

    ggapi::Struct payload =
        ggapi::Struct::create()
            .put("shape", message)
            .put(keys.serviceModelType, "aws.greengrass#SubscriptionResponseMessage");

    for(const auto &[filter, channel] : _subscriptions) {
        if(filter.match(topic)) {
            channel.write(payload);
        }
    }

    return ggapi::Struct::create()
        .put(keys.shape, ggapi::Struct::create())
        .put(keys.terminate, true);
}

ggapi::Struct LocalBroker::subscribeToTopicHandler(
    ggapi::Task, ggapi::Symbol, ggapi::Struct callData) {
    TopicFilter topic{callData.get<std::string>("topic")};

    auto channel = getScope().anchor(ggapi::Channel::create());
    {
        std::unique_lock lock(_subscriptionMutex);
        _subscriptions.emplace_back(std::move(topic), channel);
    }
    channel.addCloseCallback([this, channel]() {
        std::unique_lock lock(_subscriptionMutex);
        auto iter =
            std::find_if(_subscriptions.begin(), _subscriptions.end(), [channel](auto &&sub) {
                return sub.second == channel;
            });
        std::iter_swap(iter, std::prev(_subscriptions.end()));
        _subscriptions.pop_back();
        channel.release();
    });

    return ggapi::Struct::create()
        .put(keys.shape, ggapi::Struct::create())
        .put(keys.channel, channel);
}

bool LocalBroker::onStart(ggapi::Struct data) {
    std::ignore = getScope().subscribeToTopic(
        keys.ipcPublishToTopic,
        ggapi::TopicCallback::of(&LocalBroker::publishToTopicHandler, this));
    std::ignore = getScope().subscribeToTopic(
        keys.ipcSubscribeToTopic,
        ggapi::TopicCallback::of(&LocalBroker::subscribeToTopicHandler, this));
    return true;
}

void LocalBroker::beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) {
    std::cerr << "[local-broker] Running lifecycle phase " << phase.toString() << std::endl;
}

bool greengrass_lifecycle(uint32_t moduleHandle, uint32_t phase, uint32_t data) noexcept {
    return LocalBroker::get().lifecycle(moduleHandle, phase, data);
}
