#include "iot_broker.hpp"
#include "temp_module.hpp"
#include <cpp_api.hpp>
#include <mutex>

const IotBroker::Keys IotBroker::keys{};

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

/* Lifecycle function implementations */

void IotBroker::onInitialize(ggapi::Struct data) {
    std::cout << "[mqtt-plugin] initializing\n"; // TODO: Replace std::cout/cerr with logging
    std::ignore = util::getDeviceSdkApiHandle();
    data.put("name", "aws.greengrass.iot_broker");

    std::unique_lock guard{_mutex};
    _nucleus = data.getValue<ggapi::Struct>({"nucleus"});
    _system = data.getValue<ggapi::Struct>({"system"});
}

void IotBroker::onStart(ggapi::Struct data) {
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
    } catch(const std::exception &e) {
        std::cerr << "[mqtt-plugin] Error: " << e.what() << std::endl;
    }
    guard.unlock();

    // Fetch the initial token from TES
    // TODO: This should not be blocking
    tesOnStart(data);
    tesOnRun();
}

void IotBroker::onStop(ggapi::Struct structData) {
    // TODO: Cleanly stop thread and clean up listeners
    std::cout << "[mqtt-plugin] stopping\n";
}
