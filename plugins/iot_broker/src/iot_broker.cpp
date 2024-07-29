#include "iot_broker.hpp"
#include "containers.hpp"
#include <cpp_api.hpp>
#include <mutex>
#include <temp_module.hpp>

const IotBroker::Keys IotBroker::keys{};

void IotBroker::updateConnStatus(bool connected) {
    std::shared_lock lock{_connStatusMutex};
    _connected = connected;
    for(const auto &channel : _connStatusListeners) {
        channel.write(ggapi::Struct::create().put(keys.status, connected));
    }
}

void IotBroker::initMqtt() {
    {
        std::unique_ptr<Aws::Iot::Mqtt5ClientBuilder> builder{
            Aws::Iot::Mqtt5ClientBuilder::NewMqtt5ClientBuilderWithMtlsFromPath(
                _thingInfo.dataEndpoint, _thingInfo.certPath.c_str(), _thingInfo.keyPath.c_str())};

        if(!builder)
            throw MqttBuilderException();

        builder->WithCertificateAuthority(_thingInfo.rootCaPath.c_str());

        {
            auto connectOptions = std::make_shared<Aws::Crt::Mqtt5::ConnectPacket>();
            connectOptions->WithClientId(_thingInfo.thingName);
            builder->WithConnectOptions(std::move(connectOptions));
        }

        builder->WithClientConnectionSuccessCallback(
            [this](const Aws::Crt::Mqtt5::OnConnectionSuccessEventData &eventData) {
                util::TempModule module(getModule());
                std::cerr << "[mqtt-plugin] Connection successful with clientid "
                          << eventData.negotiatedSettings->getClientId() << "." << std::endl;
                updateConnStatus(true);
            });

        builder->WithClientConnectionFailureCallback(
            [this](const Aws::Crt::Mqtt5::OnConnectionFailureEventData &eventData) {
                util::TempModule module(getModule());
                std::cerr << "[mqtt-plugin] Connection failed: "
                          << aws_error_debug_str(eventData.errorCode) << "." << std::endl;
                updateConnStatus(false);
            });

        builder->WithClientDisconnectionCallback(
            [this](const Aws::Crt::Mqtt5::OnDisconnectionEventData &eventData) {
                util::TempModule module(getModule());
                std::cerr << "[mqtt-plugin] Disconnected." << std::endl;
                updateConnStatus(false);
            });

        builder->WithPublishReceivedCallback(
            [this](const Aws::Crt::Mqtt5::PublishReceivedEventData &eventData) {
                util::TempModule tempModule(getModule());
                if(eventData.publishPacket) {
                    auto payloadBytes =
                        Aws::Crt::ByteCursorToStringView(eventData.publishPacket->getPayload());
                    // TODO: Make this a span
                    std::string_view payload{payloadBytes.data(), payloadBytes.size()};
                    const auto &topic{eventData.publishPacket->getTopic()};

                    std::cerr << "[mqtt-plugin] Publish received on topic " << topic << ": "
                              << payload << std::endl;

                    std::shared_lock lock(_subscriptionMutex);
                    for(const auto &[filter, channel] : _subscriptions) {
                        if(filter.match(topic)) {
                            channel.write(ggapi::Struct::create()
                                              .put(keys.topicName, topic)
                                              .put(keys.payload, payload));
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

/* Lifecycle function implementations */

void IotBroker::onInitialize(ggapi::Struct data) {
    std::cout << "[mqtt-plugin] initializing\n"; // TODO: Replace std::cout/cerr with logging
    std::ignore = util::getDeviceSdkApiHandle();
    data.put("name", "aws.greengrass.iot_broker");

    std::unique_lock guard{_mutex};
    _nucleus = data.getValue<ggapi::Struct>({"nucleus"});
    _system = data.getValue<ggapi::Struct>({"system"});

    _conn = std::thread{&IotBroker::connectionThread, this, data};
}

void IotBroker::connectionThread(ggapi::Struct data) {
    util::TempModule module(getModule());
    while(true) {
        try {
            std::cout << "[mqtt-plugin] starting\n";
            std::shared_lock guard{_mutex};
            auto nucleus = _nucleus;
            auto system = _system;

            _thingInfo.rootPath = system.getValue<std::string>({"rootpath"});
            _thingInfo.rootCaPath = system.getValue<std::string>({"rootCaPath"});

            _thingInfo.certPath = system.getValue<std::string>({"certificateFilePath"});
            _thingInfo.keyPath = system.getValue<std::string>({"privateKeyPath"});
            _thingInfo.thingName = system.getValue<Aws::Crt::String>({"thingName"});
            if(_thingInfo.certPath.empty() || _thingInfo.keyPath.empty()
               || _thingInfo.thingName.empty()) {
                auto reqData = ggapi::Struct::create();
                auto respFuture = ggapi::Subscription::callTopicFirst(
                    ggapi::Symbol{keys.requestDeviceProvisionTopic}, reqData);
                if(!respFuture) {
                    // TODO: replace with better error
                    throw std::runtime_error("Failed to provision device");
                }
                auto respData = ggapi::Struct{respFuture.waitAndGetValue()};
                _thingInfo.thingName = respData.get<Aws::Crt::String>("thingName");
                system.put("thingName", _thingInfo.thingName);
                _thingInfo.keyPath = respData.get<std::string>("keyPath");
                system.put("privateKeyPath", _thingInfo.keyPath);
                _thingInfo.certPath = respData.get<std::string>("certPath");
                system.put("certificateFilePath", _thingInfo.certPath);

                auto configRef = nucleus.get<ggapi::Struct>("configuration");
                configRef.put("iotCredEndpoint", respData.get<std::string>("credEndpoint"));
                configRef.put("iotDataEndpoint", respData.get<std::string>("dataEndpoint"));
            }
            _thingInfo.credEndpoint =
                nucleus.getValue<std::string>({"configuration", "iotCredEndpoint"});
            _thingInfo.dataEndpoint =
                nucleus.getValue<std::string>({"configuration", "iotDataEndpoint"});

            initMqtt();
            if(!_worker.joinable()) {
                _worker = std::thread{&IotBroker::queueWorker, this};
            }
        } catch(const std::exception &e) {
            // TODO: Log and add backoff
            std::cerr << "[mqtt-plugin] Error: " << e.what() << std::endl;
            _client.reset();
            continue;
        } catch(...) {
            std::cerr << "[mqtt-plugin] Unknown exception." << std::endl;
            _client.reset();
            continue;
        }
        break;
    }
    tesOnStart(data);
}

ggapi::Promise IotBroker::connStatusHandler(ggapi::Symbol, const ggapi::Container &args) {
    auto promise = ggapi::Promise::create();
    promise.fulfill([&]() {
        auto channel = ggapi::Channel::create();
        std::unique_lock lock(_connStatusMutex);
        _connStatusListeners.emplace_back(channel);
        channel.addCloseCallback([this, channel]() {
            std::unique_lock lock(_connStatusMutex);
            auto iter =
                std::find(_connStatusListeners.begin(), _connStatusListeners.end(), channel);
            std::iter_swap(iter, std::prev(_connStatusListeners.end()));
            _subscriptions.pop_back();
        });

        channel.write(ggapi::Struct::create().put(keys.status, _connected));

        return ggapi::Struct::create().put(keys.channel, channel);
    });
    return promise;
}

void IotBroker::onStart(ggapi::Struct data) {
    _publishSubs = ggapi::Subscription::subscribeToTopic(
        keys.publishToIoTCoreTopic, ggapi::TopicCallback::of(&IotBroker::publishHandler, this));
    _ipcPublishSubs = ggapi::Subscription::subscribeToTopic(
        keys.ipcPublishToIoTCoreTopic,
        ggapi::TopicCallback::of(&IotBroker::ipcPublishHandler, this));
    _subscribeSubs = ggapi::Subscription::subscribeToTopic(
        keys.subscribeToIoTCoreTopic, ggapi::TopicCallback::of(&IotBroker::subscribeHandler, this));
    _ipcSubscribeSubs = ggapi::Subscription::subscribeToTopic(
        keys.ipcSubscribeToIoTCoreTopic,
        ggapi::TopicCallback::of(&IotBroker::ipcSubscribeHandler, this));
    _connStatusSubs = ggapi::Subscription::subscribeToTopic(
        keys.subscribeConnTopic, ggapi::TopicCallback::of(&IotBroker::connStatusHandler, this));
}

void IotBroker::onStop(ggapi::Struct structData) {
    // TODO: Cleanly stop thread and clean up listeners
    std::cout << "[mqtt-plugin] stopping\n";
}

void IotBroker::queueWorker() {
    util::TempModule module(getModule());
    while(true) {
        auto task = _queue.pop();
        auto event{task.get<std::string>("event")};
        auto promise{task.get<ggapi::Promise>("promise")};
        auto data{task.get<ggapi::Struct>("data")};

        if(event == "publish") {
            publishHandlerAsync(data, promise);
        } else if(event == "subscribe") {
            subscribeHandlerAsync(data, promise);
        }
    }
}
