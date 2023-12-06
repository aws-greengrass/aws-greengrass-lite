//
// Created by Julicher, Joe on 12/5/23.
//

#include "iot_broker.hpp"

ggapi::Struct IotBroker::publishHandler(ggapi::Task, ggapi::Symbol, ggapi::Struct args) {
    return get().publishHandlerImpl(args);
}

ggapi::Struct IotBroker::subscribeHandler(ggapi::Task, ggapi::Symbol, ggapi::Struct args) {
    return get().subscribeHandlerImpl(args);
}


ggapi::Struct IotBroker::publishHandlerImpl(ggapi::Struct args) {
    auto topic{args.get<std::string>(keys.topicName)};
    auto qos{args.get<int>(keys.qos)};
    auto payload{args.get<std::string>(keys.payload)};

    std::cerr << "[mqtt-plugin] Sending " << payload << " to " << topic << std::endl;

    auto onPublishComplete = [](int,
                                const std::shared_ptr<Aws::Crt::Mqtt5::PublishResult> &result) {
        if(!result->wasSuccessful()) {
            std::cerr << "[mqtt-plugin] Publish failed with error_code: " << result->getErrorCode()
                      << std::endl;
            return;
        }

        auto puback = std::dynamic_pointer_cast<Aws::Crt::Mqtt5::PubAckPacket>(result->getAck());

        if(puback) {

            if(puback->getReasonCode() == 0) {
                std::cerr << "[mqtt-plugin] Puback success" << std::endl;
            } else {
                std::cerr << "[mqtt-plugin] Puback failed: " << puback->getReasonString().value()
                          << std::endl;
            }
        }
    };

    auto publish = std::make_shared<Aws::Crt::Mqtt5::PublishPacket>(
        Aws::Crt::String(topic),
        ByteCursorFromString(Aws::Crt::String(payload)),
        static_cast<Aws::Crt::Mqtt5::QOS>(qos));

    if(!_client->Publish(publish, onPublishComplete)) {
        std::cerr << "[mqtt-plugin] Publish failed" << std::endl;
    }

    return ggapi::Struct::create();
}


ggapi::Struct IotBroker::subscribeHandlerImpl(ggapi::Struct args) {
    TopicFilter topicFilter{args.get<std::string>(keys.topicFilter)};
    int qos{args.get<int>(keys.qos)};
    ggapi::Symbol responseTopic{args.get<std::string>(keys.lpcResponseTopic)};

    std::cerr << "[mqtt-plugin] Subscribing to " << topicFilter.get() << std::endl;

    auto onSubscribeComplete = [this, topicFilter, responseTopic](
                                   int error_code,
                                   const std::shared_ptr<Aws::Crt::Mqtt5::SubAckPacket> &suback) {
        if(error_code != 0) {
            std::cerr << "[mqtt-plugin] Subscribe failed with error_code: " << error_code
                      << std::endl;
            return;
        }

        if(suback && !suback->getReasonCodes().empty()) {
            auto reasonCode = suback->getReasonCodes()[0];
            if(reasonCode >= Aws::Crt::Mqtt5::SubAckReasonCode::AWS_MQTT5_SARC_UNSPECIFIED_ERROR) {
                std::cerr << "[mqtt-plugin] Subscribe rejected with reason code: " << reasonCode
                          << std::endl;
                return;
            } else {
                std::cerr << "[mqtt-plugin] Subscribe accepted" << std::endl;
            }
        }

        {
            std::unique_lock lock(_subscriptionMutex);
            _subscriptions.insert({topicFilter, responseTopic});
        }
    };

    auto subscribe = std::make_shared<Aws::Crt::Mqtt5::SubscribePacket>();
    subscribe->WithSubscription(std::move(Aws::Crt::Mqtt5::Subscription(
        Aws::Crt::String(topicFilter.get()), static_cast<Aws::Crt::Mqtt5::QOS>(qos))));

    if(!_client->Subscribe(subscribe, onSubscribeComplete)) {
        std::cerr << "[mqtt-plugin] Subscribe failed" << std::endl;
    }

    return ggapi::Struct::create();
}

void IotBroker::initMqtt() {
    Aws::Crt::String crtEndpoint{_thingInfo.dataEndpoint};
    {
        std::unique_ptr<Aws::Iot::Mqtt5ClientBuilder> builder{
            Aws::Iot::Mqtt5ClientBuilder::NewMqtt5ClientBuilderWithMtlsFromPath(
                crtEndpoint, _thingInfo.certPath.c_str(), _thingInfo.keyPath.c_str())};

        if(!builder)
            throw mqttBuilderException();

        auto connectOptions = std::make_shared<Aws::Crt::Mqtt5::ConnectPacket>();
        connectOptions->WithClientId(
            _thingInfo.thingName.c_str()); // NOLINT(*-redundant-string-cstr)
        builder->WithConnectOptions(connectOptions);

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

                    std::string topic{eventData.publishPacket->getTopic()};
                    std::string payload{
                        reinterpret_cast<char *>(eventData.publishPacket->getPayload().ptr),
                        eventData.publishPacket->getPayload().len};

                    std::cerr << "[mqtt-plugin] Publish received on topic " << topic << ": "
                              << payload << std::endl;

                    auto response{ggapi::Struct::create()};
                    response.put(keys.topicName, topic);
                    response.put(keys.payload, payload);

                    {
                        std::shared_lock lock(_subscriptionMutex);
                        for(const auto &[key, value] : _subscriptions) {
                            if(key.match(topic)) {
                                std::ignore = ggapi::Task::sendToTopic(value, response);
                            }
                        }
                    }
                }
            });

        _client = builder->Build();
    }
    if(!_client) throw mqttClientException();
    if(!_client->Start()) throw mqttClienFailedToStart();
}

const IotBroker::Keys IotBroker::keys{};

/* Lifecycle function implementations */

void IotBroker::beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) {
    std::cerr << "[mqtt-plugin] Running lifecycle phase " << phase.toString() << std::endl;
}

void IotBroker::afterLifecycle(ggapi::Symbol phase, ggapi::Struct data) {
    std::cerr << "[mqtt-plugin] Finished lifecycle phase " << phase.toString() << std::endl;
}

bool IotBroker::onBootstrap(ggapi::Struct structData) {
    structData.put("name", "aws.greengrass.iot_broker");
    std::cout << "[mqtt-plugin] bootstrapping\n";
    return true;
}

bool IotBroker::onDiscover(ggapi::Struct data){
    return false;
}

bool IotBroker::onBind(ggapi::Struct data) {
    _nucleus = getScope().anchor(data.getValue<ggapi::Struct>({"nucleus"}));
    _system = getScope().anchor(data.getValue<ggapi::Struct>({"system"}));
    std::cout << "[mqtt-plugin] binding\n";
    return true;
}

bool IotBroker::onStart(ggapi::Struct data) {
    bool returnValue = false;
    std::cout << "[mqtt-plugin] starting\n";
    try {
        auto nucleus = _nucleus.load();
        auto system = _system.load();

        _thingInfo.rootPath = system.getValue<std::string>({"rootpath"});
        _thingInfo.rootCaPath = system.getValue<std::string>({"rootCaPath"});

        _thingInfo.certPath = system.getValue<std::string>({"certificateFilePath"});
        _thingInfo.keyPath = system.getValue<std::string>({"privateKeyPath"});
        _thingInfo.thingName = system.getValue<std::string>({"thingName"});
        if(_thingInfo.certPath.empty() || _thingInfo.keyPath.empty() || _thingInfo.thingName.empty())
        {
            auto reqData = ggapi::Struct::create();
            auto respData =
                ggapi::Task::sendToTopic(ggapi::Symbol{keys.requestDeviceProvisionTopic}, reqData);
            _thingInfo.thingName = respData.get<std::string>("thingName");
            _thingInfo.keyPath = respData.get<std::string>("keyPath");
            _thingInfo.certPath = respData.get<std::string>("certPath");
        }

        // TODO: Note, reference of the module name will be done by Nucleus, this is temporary.
        _thingInfo.credEndpoint = nucleus.getValue<std::string>({"configuration", "iotCredEndpoint"});
        _thingInfo.dataEndpoint = nucleus.getValue<std::string>({"configuration", "iotDataEndpoint"});
        initMqtt();
        std::ignore = getScope().subscribeToTopic(keys.publishToIoTCoreTopic, publishHandler);
        std::ignore = getScope().subscribeToTopic(keys.subscribeToIoTCoreTopic, subscribeHandler);
        returnValue = true;
    } catch(const std::exception &e) {
        std::cerr << "[mqtt-plugin] Error: " << e.what() << std::endl;
    }
    return returnValue;
}

bool IotBroker::onRun(ggapi::Struct data){
    return false;
}

bool IotBroker::onTerminate(ggapi::Struct structData) {
    // TODO: Cleanly stop thread and clean up listeners
    std::cout << "[mqtt-plugin] terminating\n";
    return true;
}
