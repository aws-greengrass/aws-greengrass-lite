#include "iot_broker.hpp"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cpp_api.hpp>
#include <ipc_standard_errors.hpp>
#include <mutex>
#include <variant>

static bool blockingSubscribe(
    Aws::Crt::Mqtt5::Mqtt5Client &client, std::shared_ptr<Aws::SubscribePacket> subscribeOptions) {

    std::mutex mutex;
    std::condition_variable barrier;
    std::atomic_bool success{false};

    auto onSubscribeComplete = [&barrier, &success](
                                   int error_code,
                                   const std::shared_ptr<Aws::Crt::Mqtt5::SubAckPacket> &suback) {
        auto subackReceived = [error_code, &suback]() -> bool {
            if(error_code != 0) {
                std::cerr << "[mqtt-plugin] Subscribe failed with error_code: " << error_code
                          << std::endl;
                return false;
            }

            if(suback && !suback->getReasonCodes().empty()) {
                auto reasonCode = suback->getReasonCodes().front();
                if(reasonCode
                   >= Aws::Crt::Mqtt5::SubAckReasonCode::AWS_MQTT5_SARC_UNSPECIFIED_ERROR) {
                    std::cerr << "[mqtt-plugin] Subscribe rejected with reason code: " << reasonCode
                              << std::endl;
                    return false;
                } else {
                    std::cerr << "[mqtt-plugin] Subscribe accepted" << std::endl;
                }
            }

            return true;
        };

        if(subackReceived()) {
            success.store(true, std::memory_order_relaxed);
        }
        barrier.notify_one();
    };

    if(!client.Subscribe(std::move(subscribeOptions), onSubscribeComplete)) {
        return false;
    }

    std::unique_lock guard{mutex};
    barrier.wait(guard);
    return success.load(std::memory_order_relaxed);
}

ggapi::Promise IotBroker::ipcSubscribeHandler(ggapi::Symbol, const ggapi::Container &argsBase) {
    ggapi::Struct args{argsBase};

    // TODO: We can probably remove async() here
    return ggapi::Promise::create().async(
        [this](const ggapi::Struct &args, ggapi::Promise promise) {
            promise.fulfill([&]() {
                auto ret = commonSubscribeHandler(args, [](ggapi::Struct packet) {
                    auto payload = packet.get<std::string>(keys.payload);
                    packet.put(
                        keys.payload,
                        Aws::Crt::Base64Encode(
                            Aws::Crt::Vector<uint8_t>{payload.begin(), payload.end()}));
                    return ggapi::Struct::create()
                        .put(keys.shape, ggapi::Struct::create().put(keys.message, packet))
                        .put(keys.serviceModelType, "aws.greengrass#IoTCoreMessage");
                });

                if(std::holds_alternative<ggapi::Channel>(ret)) {
                    return ggapi::Struct::create()
                        .put(keys.shape, ggapi::Struct::create())
                        .put(keys.channel, std::get<ggapi::Channel>(ret));
                }

                // TODO: error handling needs to be addressed in this function
                throw ggapi::ipc::ServiceError("Subscribe failed");
            });
        },
        args);
}

std::variant<ggapi::Channel, uint32_t> IotBroker::commonSubscribeHandler(
    const ggapi::Struct &args, PacketHandler handler) {

    TopicFilter topicFilter{args.get<Aws::Crt::String>(keys.topicName)};
    auto qos{static_cast<Aws::Crt::Mqtt5::QOS>(args.get<int>(keys.qos))};

    std::cerr << "[mqtt-plugin] Subscribing to " << topicFilter.get() << std::endl;

    auto subscribe = std::make_shared<Aws::Crt::Mqtt5::SubscribePacket>();
    subscribe->WithSubscription(Aws::Crt::Mqtt5::Subscription{topicFilter.get(), qos});

    if(!blockingSubscribe(*_client, std::move(subscribe))) {
        std::cerr << "[mqtt-plugin] Subscribe failed" << std::endl;
        return uint32_t{1};
    } else {
        std::unique_lock lock(_subscriptionMutex);
        auto channel = ggapi::Channel::create();
        _subscriptions.emplace_back(std::move(topicFilter), channel, std::move(handler));
        channel.addCloseCallback([this, channel]() {
            std::unique_lock lock(_subscriptionMutex);
            auto iter =
                std::find_if(_subscriptions.begin(), _subscriptions.end(), [channel](auto &&sub) {
                    return std::get<1>(sub) == channel;
                });
            std::iter_swap(iter, std::prev(_subscriptions.end()));
            _subscriptions.pop_back();
        });
        return channel;
    }
}

ggapi::Promise IotBroker::subscribeHandler(ggapi::Symbol, const ggapi::Container &args) {
    return ggapi::Promise::create().async(
        &IotBroker::subscribeHandlerAsync, this, ggapi::Struct(args));
}

void IotBroker::subscribeHandlerAsync(const ggapi::Struct &args, ggapi::Promise promise) {
    promise.fulfill([&]() {
        auto ret = commonSubscribeHandler(args, [](ggapi::Struct packet) { return packet; });

        if(std::holds_alternative<ggapi::Channel>(ret)) {
            return ggapi::Struct::create().put(keys.channel, std::get<ggapi::Channel>(ret));
        }

        // TODO: error handling needs to be addressed in this function
        throw ggapi::ipc::ServiceError("Subscribe failed");
    });
}
