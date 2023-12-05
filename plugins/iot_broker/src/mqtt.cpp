//
// Created by Julicher, Joe on 12/5/23.
//

#include "mqtt.h"

class mqttException: public std::exception
{
    virtual const char *what() const throw()
    {
        return "MQTT Failed to init";
    }
};

bool IotBroker::initMqtt() {
    std::cerr << "[mqtt-plugin] initMqtt." << std::endl;

    {
        Aws::Crt::String crtEndpoint{_thingInfo.dataEndpoint};
        std::unique_ptr<Aws::Iot::Mqtt5ClientBuilder> builder{
            Aws::Iot::Mqtt5ClientBuilder::NewMqtt5ClientBuilderWithMtlsFromPath(
                crtEndpoint, _thingInfo.certPath.c_str(), _thingInfo.keyPath.c_str())};

        if(!builder) {
            std::cerr << "[mqtt-plugin] Failed to set up MQTT client builder." << std::endl;
            return false;
        }

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
                if(!eventData.publishPacket) {
                    return;
                }

                std::string topic{eventData.publishPacket->getTopic()};
                std::string payload{
                    reinterpret_cast<char *>(eventData.publishPacket->getPayload().ptr),
                    eventData.publishPacket->getPayload().len};

                std::cerr << "[mqtt-plugin] Publish received on topic " << topic << ": " << payload
                          << std::endl;

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
            });

        _client = builder->Build();
    }

    if(!_client) {
        std::cerr << "[mqtt-plugin] Failed to init MQTT client: "
                  << Aws::Crt::ErrorDebugString(Aws::Crt::LastError()) << "." << std::endl;
        return false;
    }

    if(!_client->Start()) {
        std::cerr << "[mqtt-plugin] Failed to start MQTT client." << std::endl;
        return false;
    }

    return true;
}

void IotBroker::publishToProvisionPlugin() {
    auto reqData = ggapi::Struct::create();
    auto respData =
        ggapi::Task::sendToTopic(ggapi::Symbol{keys.requestDeviceProvisionTopic}, reqData);
    _thingInfo.thingName = respData.get<std::string>("thingName");
    _thingInfo.keyPath = respData.get<std::string>("keyPath");
    _thingInfo.certPath = respData.get<std::string>("certPath");
}
