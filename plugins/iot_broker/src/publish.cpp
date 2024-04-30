#include "iot_broker.hpp"
#include <atomic>
#include <condition_variable>
#include <cpp_api.hpp>
#include <ipc_standard_errors.hpp>
#include <mutex>
#include <temp_module.hpp>

ggapi::Promise IotBroker::ipcPublishHandler(
    ggapi::Symbol symbol, const ggapi::Container &argsBase) {

    ggapi::Struct args{argsBase};

    // TODO: pretty sure we can detect whether to perform this base64decode
    // from the json document. It's either bytes (base64) or plaintext
    {
        auto decoded = Aws::Crt::Base64Decode(args.get<Aws::Crt::String>(keys.payload));
        args.put(keys.payload, std::string{decoded.begin(), decoded.end()});
    }

    auto promise = publishHandler(symbol, args);

    return promise.andThen([](ggapi::Promise nextPromise, const ggapi::Future &prevFuture) {
        nextPromise.fulfill([&]() {
            auto resp = ggapi::Struct(prevFuture.getValue());
            return ggapi::Struct::create().put(keys.shape, resp).put(keys.terminate, true);
        });
    });
}

ggapi::Promise IotBroker::publishHandler(ggapi::Symbol, const ggapi::Container &args) {
    auto promise = ggapi::Promise::create();
    ggapi::Struct task = ggapi::Struct::create();
    task.put("event", "publish");
    task.put("promise", promise);
    task.put("data", ggapi::Struct(args));
    _queue.push(task);
    return promise;
}

void IotBroker::publishHandlerAsync(const ggapi::Struct &args, ggapi::Promise promise) {
    promise.fulfill([&]() {
        auto topic{args.get<Aws::Crt::String>(keys.topicName)};
        auto qos{static_cast<Aws::Crt::Mqtt5::QOS>(args.get<int>(keys.qos))};
        auto payload{args.get<Aws::Crt::String>(keys.payload)};

        std::atomic_bool success = false;

        std::cerr << "[mqtt-plugin] Sending " << payload << " to " << topic << std::endl;

        std::condition_variable barrier{};
        std::mutex mutex{};

        auto onPublishComplete =
            [&barrier,
             &success](int, const std::shared_ptr<Aws::Crt::Mqtt5::PublishResult> &result) {
                success = [&result]() -> bool {
                    if(!result->wasSuccessful()) {
                        std::cerr << "[mqtt-plugin] Publish failed with error_code: "
                                  << result->getErrorCode() << std::endl;
                        return false;
                    }

                    if(auto puback = std::dynamic_pointer_cast<Aws::Crt::Mqtt5::PubAckPacket>(
                           result->getAck())) {
                        if(puback->getReasonCode() == 0) {
                            std::cerr << "[mqtt-plugin] Puback success" << std::endl;
                        } else {
                            std::cerr << "[mqtt-plugin] Puback failed: "
                                      << puback->getReasonString().value() << std::endl;
                            return false;
                        }
                    }

                    return true;
                }();

                barrier.notify_one();
            };

        auto publish = std::make_shared<Aws::Crt::Mqtt5::PublishPacket>(
            std::move(topic), ByteCursorFromString(payload), qos);

        auto response = ggapi::Struct::create();

        if(!_client->Publish(publish, onPublishComplete)) {
            std::cerr << "[mqtt-plugin] Publish failed" << std::endl;
        } else {
            std::unique_lock lock{mutex};
            barrier.wait(lock);
        }

        if(success) {
            return ggapi::Struct::create();
        } else {
            // TODO: error handling needs to be addressed in this function
            throw ggapi::ipc::ServiceError("Publish failed");
        }
        return response;
    });
}
