#include <cpp_api.hpp>
#include <ipc_standard_errors.hpp>
#include <mqtt/topic_filter.hpp>
#include <plugin.hpp>
#include <string>
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
    ggapi::StringOrd serviceModelType{"serviceModelType"};
    ggapi::StringOrd terminate{"terminate"};
};

static const Keys keys;

class LocalBroker : public ggapi::Plugin {
private:
    std::vector<std::pair<TopicFilter<>, ggapi::Channel>> _subscriptions;
    std::shared_mutex _mutex;
    std::mutex _subscriptionMutex; // TODO: fold these two mutexes together?

    ggapi::Subscription _ipcPublishSubs;
    ggapi::Subscription _ipcSubscribeSubs;

public:
    void onStart(ggapi::Struct data) override;

    ggapi::ObjHandle publishToTopicHandler(ggapi::Symbol topic, const ggapi::Container &);

    ggapi::ObjHandle subscribeToTopicHandler(ggapi::Symbol topic, const ggapi::Container &);

    static LocalBroker &get() {
        static LocalBroker instance{};
        return instance;
    }
};

ggapi::ObjHandle LocalBroker::publishToTopicHandler(
    ggapi::Symbol, const ggapi::Container &callDataIn) {

    ggapi::Struct callData{callDataIn};
    auto topic{callData.get<std::string>(keys.topic)};
    auto message{callData.get<ggapi::Struct>(keys.publishMessage)};

    auto context{ggapi::Struct::create().put(keys.topic, topic)};

    bool isBin = message.hasKey(keys.binaryMessage);
    bool isJson = message.hasKey(keys.jsonMessage);

    if(isBin && isJson) {
        throw ggapi::ipc::InvalidArgumentsError("Both binary and JSON specified");
    }

    if(!isBin && !isJson) {
        throw ggapi::ipc::InvalidArgumentsError("Neither binary or JSON specified");
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

ggapi::ObjHandle LocalBroker::subscribeToTopicHandler(
    ggapi::Symbol, const ggapi::Container &callDataIn) {

    ggapi::Struct callData{callDataIn};
    TopicFilter topic{callData.get<std::string>("topic")};

    auto channel = ggapi::Channel::create();
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
    });

    return ggapi::Struct::create()
        .put(keys.shape, ggapi::Struct::create())
        .put(keys.channel, channel);
}

void LocalBroker::onStart(ggapi::Struct data) {
    std::unique_lock guard{_mutex};
    // TODO: These need to be closed on onStop()
    _ipcPublishSubs = ggapi::Subscription::subscribeToTopic(
        keys.ipcPublishToTopic,
        ggapi::TopicCallback::of(&LocalBroker::publishToTopicHandler, this));
    _ipcSubscribeSubs = ggapi::Subscription::subscribeToTopic(
        keys.ipcSubscribeToTopic,
        ggapi::TopicCallback::of(&LocalBroker::subscribeToTopicHandler, this));
}

extern "C" [[maybe_unused]] ggapiErrorKind greengrass_lifecycle(
    ggapiObjHandle moduleHandle, ggapiSymbol phase, ggapiObjHandle data) noexcept {
    return LocalBroker::get().lifecycle(moduleHandle, phase, data);
}
