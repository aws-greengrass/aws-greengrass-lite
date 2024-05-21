#include <cpp_api.hpp>
#include <ipc_standard_errors.hpp>
#include <mqtt/topic_filter.hpp>
#include <plugin.hpp>
#include <string>
#include <vector>

struct Keys {
    ggapi::StringOrd ipcPublishToTopic{"IPC::aws.greengrass#PublishToTopic"};
    ggapi::StringOrd ipcSubscribeToTopic{"IPC::aws.greengrass#SubscribeToTopic"};
    ggapi::StringOrd ipcPublishMetaTopic{"IPC:META::aws.greengrass#PublishToTopic"};
    ggapi::StringOrd ipcSubscribeMetaTopic{"IPC:META::aws.greengrass#SubscribeToTopic"};
    ggapi::StringOrd resource{"resource"};
    ggapi::StringOrd destination{"destination"};
    ggapi::StringOrd publishToTopic{"aws.greengrass.PublishToTopic"};
    ggapi::StringOrd subscribeToTopic{"aws.greengrass.SubscribeToTopic"};
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
    ggapi::StringOrd ipcServiceName{"aws.greengrass.ipc.pubsub"};
};

static const Keys keys;

class LocalBroker : public ggapi::Plugin {
private:
    std::vector<std::pair<TopicFilter<>, ggapi::Channel>> _subscriptions;
    std::shared_mutex _mutex;
    std::mutex _subscriptionMutex; // TODO: fold these two mutexes together?

    ggapi::Subscription _publishSubs;
    ggapi::Subscription _subscribeSubs;
    ggapi::Subscription _ipcPublishSubs;
    ggapi::Subscription _ipcSubscribeSubs;
    ggapi::Subscription _ipcPublishMetaSubs;
    ggapi::Subscription _ipcSubscribeMetaSubs;

public:
    void onStart(ggapi::Struct data) override;

    ggapi::ObjHandle publishToTopicHandler(ggapi::Symbol topic, const ggapi::Container &);
    ggapi::ObjHandle subscribeToTopicHandler(ggapi::Symbol topic, const ggapi::Container &);
    ggapi::ObjHandle getAuthZMetaData(ggapi::Symbol topic, const ggapi::Container &);
    ggapi::ObjHandle ipcPublishToTopicHandler(ggapi::Symbol topic, const ggapi::Container &);
    ggapi::ObjHandle ipcSubscribeToTopicHandler(ggapi::Symbol topic, const ggapi::Container &);

    static LocalBroker &get() {
        static LocalBroker instance{};
        return instance;
    }
};

ggapi::ObjHandle LocalBroker::ipcPublishToTopicHandler(
    ggapi::Symbol topic, const ggapi::Container &callDataIn) {
    auto ret = publishToTopicHandler(topic, callDataIn);

    return ggapi::Struct::create().put(keys.shape, ret).put(keys.terminate, true);
}

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

    for(const auto &[filter, channel] : _subscriptions) {
        if(filter.match(topic)) {
            channel.write(message);
        }
    }

    return ggapi::Struct::create();
}

ggapi::ObjHandle LocalBroker::ipcSubscribeToTopicHandler(
    ggapi::Symbol topic, const ggapi::Container &callDataIn) {

    ggapi::Struct resp{subscribeToTopicHandler(topic, callDataIn)};
    ggapi::Channel channel{resp.get<ggapi::Channel>("channel")};

    auto filteredChannel = ggapi::Channel::create();
    channel.addListenCallback(ggapi::ChannelListenCallback::of<ggapi::Struct>(
        [filteredChannel](const ggapi::Struct &message) {
            filteredChannel.write(
                ggapi::Struct::create()
                    .put("shape", message)
                    .put(keys.serviceModelType, "aws.greengrass#SubscriptionResponseMessage"));
        }));

    return ggapi::Struct::create()
        .put(keys.shape, ggapi::Struct::create())
        .put(keys.channel, filteredChannel);
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

    return ggapi::Struct::create().put(keys.channel, channel);
}

ggapi::ObjHandle LocalBroker::getAuthZMetaData(ggapi::Symbol, const ggapi::Container &callDataIn) {
    ggapi::Struct callData{callDataIn};

    auto topic = callData.get<std::string>("topic");
    return ggapi::Struct::create()
        .put(keys.destination, keys.ipcServiceName.toString())
        .put(keys.resource, topic);
}

void LocalBroker::onStart(ggapi::Struct data) {
    std::unique_lock guard{_mutex};
    // TODO: These need to be closed on onStop()
    _ipcPublishMetaSubs = ggapi::Subscription::subscribeToTopic(
        keys.ipcPublishMetaTopic, ggapi::TopicCallback::of(&LocalBroker::getAuthZMetaData, this));
    _ipcSubscribeMetaSubs = ggapi::Subscription::subscribeToTopic(
        keys.ipcSubscribeMetaTopic, ggapi::TopicCallback::of(&LocalBroker::getAuthZMetaData, this));
    _publishSubs = ggapi::Subscription::subscribeToTopic(
        keys.publishToTopic, ggapi::TopicCallback::of(&LocalBroker::publishToTopicHandler, this));
    _subscribeSubs = ggapi::Subscription::subscribeToTopic(
        keys.subscribeToTopic,
        ggapi::TopicCallback::of(&LocalBroker::subscribeToTopicHandler, this));
    _ipcPublishSubs = ggapi::Subscription::subscribeToTopic(
        keys.ipcPublishToTopic,
        ggapi::TopicCallback::of(&LocalBroker::ipcPublishToTopicHandler, this));
    _ipcSubscribeSubs = ggapi::Subscription::subscribeToTopic(
        keys.ipcSubscribeToTopic,
        ggapi::TopicCallback::of(&LocalBroker::ipcSubscribeToTopicHandler, this));
}

extern "C" [[maybe_unused]] ggapiErrorKind greengrass_lifecycle(
    ggapiObjHandle moduleHandle, ggapiSymbol phase, ggapiObjHandle data) noexcept {
    return LocalBroker::get().lifecycle(moduleHandle, phase, data);
}
