#include "channel.hpp"
#include "containers.hpp"
#include <cpp_api.hpp>
#include <plugin.hpp>

const static auto LOG = ggapi::Logger::of("MqttStatusBridge");

struct Keys {
    ggapi::Symbol subscribeConnTopic{"aws.greengrass.SubscribeConnStatus"};
    ggapi::StringOrd publishToTopic{"aws.greengrass.PublishToTopic"};
    ggapi::Symbol channel{"channel"};
    ggapi::Symbol status{"status"};
    ggapi::Symbol errorCode{"errorCode"};
};

static const Keys keys;

class MqttStatusBridge : public ggapi::Plugin {
private:
public:
    void onStart(ggapi::Struct data) override;

    static MqttStatusBridge &get() {
        static MqttStatusBridge instance{};
        return instance;
    }
};

void MqttStatusBridge::onStart(ggapi::Struct data) {
    auto responseFuture =
        ggapi::Subscription::callTopicFirst(keys.subscribeConnTopic, ggapi::Struct::create());
    if(!responseFuture) {
        LOG.atError("conn-status").log("Subscribe to conn status failed.");
    } else {
        responseFuture.whenValid([](const ggapi::Future &completedFuture) {
            auto response = ggapi::Struct(completedFuture.getValue());
            auto channel = response.get<ggapi::Channel>(keys.channel);
            channel.addListenCallback(
                ggapi::ChannelListenCallback::of<ggapi::Struct>([](const ggapi::Struct &response) {
                    auto status = response.get<bool>(keys.status);
                    auto payload =
                        ggapi::Struct::create()
                            .put("topic", "/greengrass/connection-status")
                            .put(
                                "publishMessage",
                                ggapi::Struct::create().put(
                                    "jsonMessage",
                                    status ? "{\"connected\":true}" : "{\"connected\":false}"));
                    auto pubFuture =
                        ggapi::Subscription::callTopicFirst(keys.publishToTopic, payload);
                    if(pubFuture) {
                        pubFuture.whenValid([](const ggapi::Future &) {});
                    }
                }));
        });
    }
}

extern "C" [[maybe_unused]] ggapiErrorKind greengrass_lifecycle(
    ggapiObjHandle moduleHandle, ggapiSymbol phase, ggapiObjHandle data) noexcept {
    return MqttStatusBridge::get().lifecycle(moduleHandle, phase, data);
}
