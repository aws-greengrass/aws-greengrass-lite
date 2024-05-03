#include "iot_broker.hpp"
#include <cpp_api.hpp>
#include <ipc_standard_errors.hpp>
#include <string>
#include <temp_module.hpp>

ggapi::Promise IotBroker::ipcSubscribeHandler(ggapi::Symbol symbol, const ggapi::Container &args) {
    auto promise = subscribeHandler(symbol, args);
    return promise.andThen([](ggapi::Promise nextPromise, const ggapi::Future &prevFuture) {
        nextPromise.fulfill([&]() {
            auto resp = ggapi::Struct(prevFuture.getValue());
            resp.put(keys.shape, ggapi::Struct::create());

            auto channel = resp.get<ggapi::Channel>(keys.channel);
            auto filteredChannel = ggapi::Channel::create();
            resp.put(keys.channel, filteredChannel);

            channel.addListenCallback(ggapi::ChannelListenCallback::of<ggapi::Struct>(
                [filteredChannel](const ggapi::Struct &packet) {
                    auto payload = packet.get<std::string>(keys.payload);
                    auto message = ggapi::Struct::create()
                                       .put(keys.topicName, packet.get<std::string>(keys.topicName))
                                       .put(
                                           keys.payload,
                                           Aws::Crt::Base64Encode(Aws::Crt::Vector<uint8_t>{
                                               payload.begin(), payload.end()}));
                    return ggapi::Struct::create()
                        .put(keys.shape, ggapi::Struct::create().put(keys.message, message))
                        .put(keys.serviceModelType, "aws.greengrass#IoTCoreMessage");
                }));
            return resp;
        });
    });
}

ggapi::Promise IotBroker::subscribeHandler(ggapi::Symbol, const ggapi::Container &args) {
    auto promise = ggapi::Promise::create();
    ggapi::Struct task = ggapi::Struct::create();
    task.put("event", "subscribe");
    task.put("promise", promise);
    task.put("data", ggapi::Struct(args));
    _queue.push(task);
    return promise;
}

void IotBroker::subscribeHandlerAsync(const ggapi::Struct &args, ggapi::Promise promise) {
    try {
        TopicFilter topicFilter{args.get<Aws::Crt::String>(keys.topicName)};
        auto qos{static_cast<Aws::Crt::Mqtt5::QOS>(args.get<int>(keys.qos))};

        std::cerr << "[mqtt-plugin] Subscribing to " << topicFilter.get() << std::endl;

        auto subscribe = std::make_shared<Aws::Crt::Mqtt5::SubscribePacket>();
        subscribe->WithSubscription(Aws::Crt::Mqtt5::Subscription{topicFilter.get(), qos});

        auto onSubscribeComplete =
            [this, promise, topicFilter](
                int error_code,
                const std::shared_ptr<Aws::Crt::Mqtt5::SubAckPacket> &suback) mutable {
                util::TempModule module(getModule());
                promise.fulfill([&]() {
                    if(error_code != 0) {
                        std::cerr << "[mqtt-plugin] Subscribe failed with error_code: "
                                  << error_code << std::endl;
                        throw ggapi::ipc::ServiceError("Subscribe failed");
                    } else if(suback && !suback->getReasonCodes().empty()) {
                        auto reasonCode = suback->getReasonCodes().front();
                        if(reasonCode
                           >= Aws::Crt::Mqtt5::SubAckReasonCode::AWS_MQTT5_SARC_UNSPECIFIED_ERROR) {
                            std::cerr << "[mqtt-plugin] Subscribe rejected with reason code: "
                                      << reasonCode << std::endl;
                            throw ggapi::ipc::ServiceError("Subscribe failed");
                        } else {
                            std::cerr << "[mqtt-plugin] Subscribe accepted" << std::endl;
                        }
                    }

                    auto channel = ggapi::Channel::create();
                    std::unique_lock lock(_subscriptionMutex);
                    _subscriptions.emplace_back(std::move(topicFilter), channel);
                    channel.addCloseCallback([this, channel]() {
                        std::unique_lock lock(_subscriptionMutex);
                        auto iter = std::find_if(
                            _subscriptions.begin(), _subscriptions.end(), [channel](auto &&sub) {
                                return std::get<1>(sub) == channel;
                            });
                        std::iter_swap(iter, std::prev(_subscriptions.end()));
                        _subscriptions.pop_back();
                    });

                    return ggapi::Struct::create().put(keys.channel, channel);
                });
            };

        if(!_client->Subscribe(std::move(subscribe), onSubscribeComplete)) {
            throw ggapi::ipc::ServiceError("Subscribe failed");
        }
    } catch(...) {
        promise.setError(ggapi::GgApiError::of(std::current_exception()));
    }
}
