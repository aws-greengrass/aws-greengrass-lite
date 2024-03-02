#include "iot_broker.hpp"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cpp_api.hpp>
#include <mutex>
#include <variant>

ggapi::Promise IotBroker::ipcPublishHandler(
    ggapi::Symbol symbol, const ggapi::Container &argsBase) {

    ggapi::Struct args{argsBase};

    // TODO: pretty sure we can detect whether to perform this base64decode
    // from the json document. It's either bytes (base64) or plaintext
    {
        auto decoded = Aws::Crt::Base64Decode(args.get<Aws::Crt::String>(keys.payload));
        args.put(keys.payload, std::string{decoded.begin(), decoded.end()});
    }
    auto nestedPromise = publishHandler(symbol, args);
    return nestedPromise.andThen([](ggapi::Promise nextPromise, const ggapi::Future &prevFuture) {
        auto resp = ggapi::Struct(prevFuture.getValue());
        nextPromise.setValue(
            ggapi::Struct::create().put(keys.shape, resp).put(keys.terminate, true));
    });
}

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
                promise.setValue(ggapi::Struct::create()
                                     .put(keys.shape, ggapi::Struct::create())
                                     .put(keys.channel, std::get<ggapi::Channel>(ret)));
                return;
            }

            // TODO: replace with setError
            promise.setValue(ggapi::Struct::create().put(keys.errorCode, std::get<uint32_t>(ret)));
        },
        args);
}

ggapi::Promise IotBroker::publishHandler(ggapi::Symbol, const ggapi::Container &args) {
    return ggapi::Promise::create().async(
        &IotBroker::publishHandlerAsync, this, ggapi::Struct(args));
}

void IotBroker::publishHandlerAsync(const ggapi::Struct &args, ggapi::Promise promise) {

    auto topic{args.get<Aws::Crt::String>(keys.topicName)};
    auto qos{static_cast<Aws::Crt::Mqtt5::QOS>(args.get<int>(keys.qos))};
    auto payload{args.get<Aws::Crt::String>(keys.payload)};

    std::atomic_bool success = false;

    std::cerr << "[mqtt-plugin] Sending " << payload << " to " << topic << std::endl;

    std::condition_variable barrier{};
    std::mutex mutex{};

    auto onPublishComplete =
        [&barrier, &success](int, const std::shared_ptr<Aws::Crt::Mqtt5::PublishResult> &result) {
            success = [&result]() -> bool {
                if(!result->wasSuccessful()) {
                    std::cerr << "[mqtt-plugin] Publish failed with error_code: "
                              << result->getErrorCode() << std::endl;
                    return false;
                }

                if(auto puback =
                       std::dynamic_pointer_cast<Aws::Crt::Mqtt5::PubAckPacket>(result->getAck())) {
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

    // TODO - setValue on success, setError on error
    response.put(keys.errorCode, !success);
    promise.setValue(response);
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

    auto ret = commonSubscribeHandler(args, [](ggapi::Struct packet) { return packet; });

    if(std::holds_alternative<ggapi::Channel>(ret)) {
        promise.setValue(ggapi::Struct::create().put(keys.channel, std::get<ggapi::Channel>(ret)));
        return;
    }

    // TODO: replace with setError
    promise.setValue(ggapi::Struct::create().put(keys.errorCode, std::get<uint32_t>(ret)));
}

void IotBroker::initMqtt() {
    {
        std::unique_ptr<Aws::Iot::Mqtt5ClientBuilder> builder{
            Aws::Iot::Mqtt5ClientBuilder::NewMqtt5ClientBuilderWithMtlsFromPath(
                _thingInfo.dataEndpoint, _thingInfo.certPath.c_str(), _thingInfo.keyPath.c_str())};

        if(!builder)
            throw MqttBuilderException();

        {
            auto connectOptions = std::make_shared<Aws::Crt::Mqtt5::ConnectPacket>();
            connectOptions->WithClientId(_thingInfo.thingName);
            builder->WithConnectOptions(std::move(connectOptions));
        }

        builder->WithClientConnectionSuccessCallback(
            [](const Aws::Crt::Mqtt5::OnConnectionSuccessEventData &eventData) {
                std::cerr << "[mqtt-plugin] Connection successful with clientid "
                          << eventData.negotiatedSettings->getClientId() << "." << std::endl;
            });

        builder->WithClientConnectionFailureCallback(
            [](const Aws::Crt::Mqtt5::OnConnectionFailureEventData &eventData) {
                std::cerr << "[mqtt-plugin] Connection failed: "
                          << aws_error_debug_str(eventData.errorCode) << "." << std::endl;
            });

        builder->WithPublishReceivedCallback(
            [this](const Aws::Crt::Mqtt5::PublishReceivedEventData &eventData) {
                if(eventData.publishPacket) {
                    auto payloadBytes =
                        Aws::Crt::ByteCursorToStringView(eventData.publishPacket->getPayload());
                    // TODO: Make this a span
                    std::string_view payload{payloadBytes.data(), payloadBytes.size()};
                    const auto &topic{eventData.publishPacket->getTopic()};

                    std::cerr << "[mqtt-plugin] Publish received on topic " << topic << ": "
                              << payload << std::endl;

                    std::shared_lock lock(_subscriptionMutex);
                    for(const auto &[filter, channel, packetHandler] : _subscriptions) {
                        if(filter.match(topic)) {
                            auto response{ggapi::Struct::create()};
                            response.put(keys.topicName, topic);
                            response.put(keys.payload, payload);

                            auto finalResp = packetHandler(response);
                            channel.write(finalResp);
                        }
                    }
                }
            });

        _client = builder->Build();
    }
    if(!_client)
        throw MqttClientException();
    if(!_client->Start())
        throw MqttClientFailedToStart();
}

const IotBroker::Keys IotBroker::keys{};

// Initializes global CRT API
// TODO: This should be extern
static Aws::Crt::ApiHandle apiHandle{};

/* Lifecycle function implementations */

bool IotBroker::onInitialize(ggapi::Struct data) {
    data.put("name", "aws.greengrass.iot_broker");
    std::cout << "[mqtt-plugin] initializing\n";

    // activate the logging.  Probably not going to stay here so don't worry if you see it fail to
    // initialize
    static std::once_flag loggingInitialized;
    try {
        std::call_once(loggingInitialized, []() {
            apiHandle.InitializeLogging(Aws::Crt::LogLevel::Info, stderr);
        });
    } catch(const std::exception &e) {
        std::cerr << "[mqtt-plugin] probably did not initialize the logging: " << e.what()
                  << std::endl;
    }

    std::unique_lock guard{_mutex};
    _nucleus = data.getValue<ggapi::Struct>({"nucleus"});
    _system = data.getValue<ggapi::Struct>({"system"});
    return true;
}

bool IotBroker::onStart(ggapi::Struct data) {
    bool returnValue = false;
    std::cout << "[mqtt-plugin] starting\n";
    std::shared_lock guard{_mutex};
    try {
        auto nucleus = _nucleus;
        auto system = _system;

        _thingInfo.rootPath = system.getValue<std::string>({"rootpath"});
        _thingInfo.rootCaPath = system.getValue<std::string>({"rootCaPath"});

        _thingInfo.certPath = system.getValue<std::string>({"certificateFilePath"});
        _thingInfo.keyPath = system.getValue<std::string>({"privateKeyPath"});
        _thingInfo.thingName = system.getValue<Aws::Crt::String>({"thingName"});
        // TODO: Lots of logic here can block onStart - needs to be made async
        if(_thingInfo.certPath.empty() || _thingInfo.keyPath.empty()
           || _thingInfo.thingName.empty()) {
            auto reqData = ggapi::Struct::create();
            auto respFuture = ggapi::Subscription::callTopicFirst(
                ggapi::Symbol{keys.requestDeviceProvisionTopic}, reqData);
            if(!respFuture) {
                // TODO: replace with better error
                throw std::runtime_error("Failed to provision device");
            }
            // TODO: This should not block onStart
            auto respData = ggapi::Struct{respFuture.waitAndGetValue()};
            _thingInfo.thingName = respData.get<Aws::Crt::String>("thingName");
            _thingInfo.keyPath = respData.get<std::string>("keyPath");
            _thingInfo.certPath = respData.get<std::string>("certPath");
        }

        // TODO: Note, reference of the module name will be done by Nucleus, this is temporary.
        _thingInfo.credEndpoint =
            nucleus.getValue<std::string>({"configuration", "iotCredEndpoint"});
        _thingInfo.dataEndpoint =
            nucleus.getValue<std::string>({"configuration", "iotDataEndpoint"});
        initMqtt();
        _publishSubs = ggapi::Subscription::subscribeToTopic(
            keys.publishToIoTCoreTopic, ggapi::TopicCallback::of(&IotBroker::publishHandler, this));
        _ipcPublishSubs = ggapi::Subscription::subscribeToTopic(
            keys.ipcPublishToIoTCoreTopic,
            ggapi::TopicCallback::of(&IotBroker::ipcPublishHandler, this));
        _subscribeSubs = ggapi::Subscription::subscribeToTopic(
            keys.subscribeToIoTCoreTopic,
            ggapi::TopicCallback::of(&IotBroker::subscribeHandler, this));
        _ipcSubscribeSubs = ggapi::Subscription::subscribeToTopic(
            keys.ipcSubscribeToIoTCoreTopic,
            ggapi::TopicCallback::of(&IotBroker::ipcSubscribeHandler, this));
        returnValue = true;
    } catch(const std::exception &e) {
        std::cerr << "[mqtt-plugin] Error: " << e.what() << std::endl;
    }
    guard.unlock();

    // Fetch the initial token from TES
    // TODO: This should not be blocking
    tesOnStart(data);
    tesOnRun();
    return returnValue;
}

bool IotBroker::onStop(ggapi::Struct structData) {
    // TODO: Cleanly stop thread and clean up listeners
    std::cout << "[mqtt-plugin] stopping\n";
    return true;
}

bool IotBroker::onError_stop(ggapi::Struct data) {
    // TODO: Cleanly stop threads and remove listenters.
    // This plugin is going to be in the broken state when this finishes.
    return true;
}
