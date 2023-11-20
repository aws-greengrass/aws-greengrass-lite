#include "device_provisioning.hpp"
#include <iostream>
#include <chrono>

using namespace std::chrono_literals;

/**
 *
 * @param phase
 * @param data
 */
void ProvisionPlugin::beforeLifecycle(ggapi::StringOrd phase, ggapi::Struct data) {
    std::cout << "[provision-plugin] Running lifecycle provision plugin... " << ggapi::StringOrd{phase}.toString()
              << std::endl;
}

/**
 *
 * @param task
 * @param topic
 * @param callData
 * @return
 */
ggapi::Struct ProvisionPlugin::testListener(ggapi::Task task, ggapi::StringOrd topic, ggapi::Struct callData) {
    auto configStruct = callData.getValue<ggapi::Struct>({"config"});

    // TODO: Alternative rootCa provisioning
    DeviceConfig deviceConfig;
    deviceConfig.templateName = configStruct.getValue<std::string>({"templateName"});
    deviceConfig.claimKeyPath = configStruct.getValue<std::string>({"claimKeyPath"});
    deviceConfig.claimCertPath = configStruct.getValue<std::string>({"claimCertPath"});
    deviceConfig.endpoint = configStruct.getValue<std::string>({"endpoint"});
    deviceConfig.rootPath = configStruct.getValue<std::string>({"rootPath"});
    deviceConfig.templateParams = configStruct.getValue<std::string>({"templateParams"});
//    deviceConfig.rootCaPath = configStruct.getValue<std::string>({"rootCaPath"});
//    deviceConfig.awsRegion = configStruct.getValue<std::string>(
//        {"awsRegion"}
//    );
    deviceConfig.deviceId = Aws::Crt::UUID().ToString();
//    deviceConfig.mqttPort = 80; // https - 443 and http - 80

    auto deviceProvisioning = std::make_unique<ProvisionPlugin>();
    deviceProvisioning->setDeviceConfig(deviceConfig);
    return deviceProvisioning->provisionDevice();
}

/**
 *
 * @param data
 * @return
 */
bool ProvisionPlugin::onStart(ggapi::Struct data) {
    std::ignore = getScope().subscribeToTopic(ggapi::StringOrd{"aws.greengrass.provisioning"}, testListener);
    return true;
}

/**
 *
 * @param data
 * @return
 */
bool ProvisionPlugin::onRun(ggapi::Struct data) {
    return true;
}

/**
 * Provision device with csr or create key and certificate with certificate authority
 * @return
 */
ggapi::Struct ProvisionPlugin::provisionDevice() {
//        Aws::Crt::Io::EventLoopGroup eventLoopGroup(1, allocator);
//        Aws::Crt::Io::DefaultHostResolver hostResolver(eventLoopGroup, 8, 30, allocator);
//        Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, hostResolver);
        if (initMqtt()) {
            try {
                generateCredentials();
                ggapi::Struct response = ggapi::Struct::create();
                response.put("thingName", _thingName);
                response.put("keyPath", _keyPath.c_str());
                response.put("certPath", _certPath.c_str());
                return response;
            }
            catch (const std::exception &e) {
                std::cerr<<"[provision-plugin] Error while provisioning the device\n";
                throw e;
            }
        }
        else {
            throw std::runtime_error("[provision-plugin] Unable to initialize the mqtt client\n");
        }
}

/**
 *
 * @param deviceConfig
 */
void ProvisionPlugin::setDeviceConfig(const DeviceConfig &deviceConfig) {
    _deviceConfig = deviceConfig;
    if(_deviceConfig.templateName.empty()) {
        throw std::runtime_error("Template name not found");
    }
    if(_deviceConfig.claimCertPath.empty()) {
        throw std::runtime_error("Path to claim certificate not found");
    }
    if(_deviceConfig.claimKeyPath.empty()) {
        throw std::runtime_error("Path to claim private key not found");
    }
    //    if(_deviceConfig.rootCaPath.empty()) {
    //        throw std::runtime_error("Path to root CA not found");
    //    }
    if(_deviceConfig.endpoint.empty()) {
        throw std::runtime_error("IoT endpoint not found");
    }
    if(_deviceConfig.rootPath.empty()) {
        throw std::runtime_error("Root path for greengrass not found");
    }
    _keyPath = std::filesystem::path(_deviceConfig.rootPath) / PRIVATE_KEY_PATH_RELATIVE_TO_ROOT;
    _certPath = std::filesystem::path(_deviceConfig.rootPath) / DEVICE_CERTIFICATE_PATH_RELATIVE_TO_ROOT;
}

/**
 *
 * @return
 */
bool ProvisionPlugin::initMqtt() {
    struct aws_allocator *allocator = aws_default_allocator();

    std::promise<bool> connectionPromise;
    std::promise<void> stoppedPromise;
    std::promise<void> disconnectPromise;
    std::promise<bool> subscribeSuccess;

    std::unique_ptr<Aws::Iot::Mqtt5ClientBuilder> builder{
        Aws::Iot::Mqtt5ClientBuilder::NewMqtt5ClientBuilderWithMtlsFromPath(
            _deviceConfig.endpoint, _deviceConfig.claimCertPath.c_str(), _deviceConfig.claimKeyPath.c_str()
                )};

    if (builder == nullptr)
    {
        std::cerr<<"Failed to setup MQTT client builder "<<Aws::Crt::LastError()<<": "<<Aws::Crt::ErrorDebugString(Aws::Crt::LastError())<<std::endl;
        return false;
    }


//    builder->WithCertificateAuthority(_deviceConfig.rootCaPath.c_str());
    if (_deviceConfig.mqttPort.has_value()) {
        builder->WithPort(static_cast<uint16_t>(_deviceConfig.mqttPort.value()));
    }
    else {
        builder->WithPort(433);
    }

    // http proxy
    if (_deviceConfig.proxyUrl.has_value() && _deviceConfig.proxyUsername.has_value() && _deviceConfig.proxyPassword.has_value() && _deviceConfig.proxyPort.has_value()) {
        Aws::Crt::Http::HttpClientConnectionProxyOptions proxyOptions;
        proxyOptions.HostName = _deviceConfig.endpoint;
        proxyOptions.Port = _deviceConfig.proxyPort.value();
        proxyOptions.ProxyConnectionType = Aws::Crt::Http::AwsHttpProxyConnectionType::Tunneling;

        Aws::Crt::Io::TlsConnectionOptions tlsConnectionOptions;
        Aws::Crt::Io::TlsContextOptions proxyTlsCtxOptions = Aws::Crt::Io::TlsContextOptions::InitDefaultClient();
        proxyTlsCtxOptions.SetVerifyPeer(false);

        std::shared_ptr<Aws::Crt::Io::TlsContext> proxyTlsContext =
            Aws::Crt::MakeShared<Aws::Crt::Io::TlsContext>(allocator, proxyTlsCtxOptions, Aws::Crt::Io::TlsMode::CLIENT, allocator);
        tlsConnectionOptions = proxyTlsContext->NewConnectionOptions();
        Aws::Crt::ByteCursor proxyName = ByteCursorFromString(proxyOptions.HostName);
        tlsConnectionOptions.SetServerName(proxyName);

        proxyOptions.TlsOptions = tlsConnectionOptions;

        proxyOptions.AuthType = Aws::Crt::Http::AwsHttpProxyAuthenticationType::Basic;
        proxyOptions.BasicAuthUsername = *_deviceConfig.proxyUsername;
        proxyOptions.BasicAuthPassword = *_deviceConfig.proxyPassword;
        builder->WithHttpProxyOptions(proxyOptions);
    }

    // connection options
    auto connectOptions = std::make_shared<Aws::Crt::Mqtt5::ConnectPacket>();
    if (_deviceConfig.deviceId.has_value()) {
        connectOptions->WithClientId(_deviceConfig.deviceId.value());
    }
    builder->WithConnectOptions(connectOptions);

    // register callbacks
    builder->WithClientConnectionSuccessCallback(
        [&connectionPromise](const Aws::Crt::Mqtt5::OnConnectionSuccessEventData &eventData) {
            std::cerr << "[provision-plugin] Connection successful with clientid "
                      << eventData.negotiatedSettings->getClientId() << "." << std::endl;
            connectionPromise.set_value(true);
        }
    );

    builder->WithClientConnectionFailureCallback(
        [&connectionPromise](const Aws::Crt::Mqtt5::OnConnectionFailureEventData &eventData) {
            std::cerr << "[provision-plugin] Connection failed: "
                      << aws_error_debug_str(eventData.errorCode) << "." << std::endl;
            connectionPromise.set_value(false);
        }
    );

    builder->WithClientStoppedCallback([&stoppedPromise](const Aws::Crt::Mqtt5::OnStoppedEventData &) {
        std::cout<<"[provision-plugin] Mqtt client stopped\n";
    });

    builder->WithClientAttemptingConnectCallback([](const Aws::Crt::Mqtt5::OnAttemptingConnectEventData &) {
        std::cout<<"[provision-plugin] Attempting to connect...\n";
    });

    builder->WithClientDisconnectionCallback([](const Aws::Crt::Mqtt5::OnDisconnectionEventData &eventData) {
        std::cout<<"[provision-plugin] Mqtt client disconnected\n"<<aws_error_debug_str(eventData.errorCode);
    });

    _mqttClient = builder->Build();

    if(!_mqttClient) {
        std::cerr << "[provision-plugin] Failed to init MQTT client: "
                  << Aws::Crt::ErrorDebugString(Aws::Crt::LastError()) << "." << std::endl;
        return false;
    }

    if(!_mqttClient->Start()) {
        std::cerr << "[provision-plugin] Failed to start MQTT client." << std::endl;
        return false;
    }

    if(!connectionPromise.get_future().get()) {
        return false;
    }
    return true;
}
/**
 * Obtain credentials from AWS IoT
 */
void ProvisionPlugin::generateCredentials() {

    _identityClient = std::make_shared<Aws::Iotidentity::IotIdentityClient>(_mqttClient);
    Aws::Iotidentity::IotIdentityClient identityClient(_mqttClient);

    if (_deviceConfig.csrPath->empty()) {
        std::promise<void> keysPublishCompletedPromise;
        std::promise<void> keysAcceptedCompletedPromise;
        std::promise<void> keysRejectedCompletedPromise;

        Aws::Crt::String token;

        auto onKeysPublishSubAck = [&](int ioErr) {
            if (ioErr != AWS_OP_SUCCESS)
            {
                std::cerr<<"[provision-plugin] Error publishing to CreateKeysAndCertificate: %s\n"<<Aws::Crt::ErrorDebugString(ioErr);
                return;
            }

            keysPublishCompletedPromise.set_value();
        };

        auto onKeysAcceptedSubAck = [&](int ioErr) {
            if (ioErr != AWS_OP_SUCCESS)
            {
                std::cerr<<"[provision-plugin] Error subscribing to CreateKeysAndCertificate accepted: %s\n"<<Aws::Crt::ErrorDebugString(ioErr);
                return;
            }

            keysAcceptedCompletedPromise.set_value();
        };

        auto onKeysRejectedSubAck = [&](int ioErr) {
            if (ioErr != AWS_OP_SUCCESS)
            {
                std::cerr<<"[provision-plugin] Error subscribing to CreateKeysAndCertificate rejected: %s\n"<<Aws::Crt::ErrorDebugString(ioErr);
                return;
            }
            keysRejectedCompletedPromise.set_value();
        };

        auto onKeysAccepted = [&](Aws::Iotidentity::CreateKeysAndCertificateResponse *response, int ioErr) {
            if(ioErr == AWS_OP_SUCCESS) {
                try {
                    std::filesystem::path rootPath = _deviceConfig.rootPath;

                    // Write key and certificate to path
                    std::filesystem::path certPath =
                        rootPath / DEVICE_CERTIFICATE_PATH_RELATIVE_TO_ROOT;
                    std::ofstream certStream(certPath);
                    if(certStream.is_open()) {
                        certStream << response->CertificatePem->c_str();
                    }
                    certStream.close();

                    std::filesystem::path keyPath = rootPath / PRIVATE_KEY_PATH_RELATIVE_TO_ROOT;
                    std::ofstream keyStream(keyPath);
                    if(keyStream.is_open()) {
                        keyStream << response->PrivateKey->c_str();
                    }
                    keyStream.close();

                    std::cerr << "[provision-plugin] certificateId: %s.\n" << response->CertificateId->c_str();
                    token = *response->CertificateOwnershipToken;
                }
                catch (std::exception &e) {
                    std::cerr<<"[provision-plugin] Error while writing keys and certificate to root path: %s.\n"<<e.what();
                }
            }
            else
            {
                std::cerr<<"[provision-plugin] Error on subscription: %s.\n"<<Aws::Crt::ErrorDebugString(ioErr);
                return;
            }
        };

        auto onKeysRejected = [&](Aws::Iotidentity::ErrorResponse *error, int ioErr) {
            if (ioErr == AWS_OP_SUCCESS)
            {
                std::cout<<"[provision-plugin] CreateKeysAndCertificate failed with statusCode %d, errorMessage %s and errorCode %s."
                        <<*error->StatusCode<<error->ErrorMessage->c_str()<<error->ErrorCode->c_str();
                return;
            }
            else
            {
                std::cerr<<"[provision-plugin] Error on subscription: %s.\n"<<Aws::Crt::ErrorDebugString(ioErr);
                return;
            }
        };

        std::cout << "[provision-plugin] Subscribing to CreateKeysAndCertificate Accepted and Rejected topics" << std::endl;
        Aws::Iotidentity::CreateKeysAndCertificateSubscriptionRequest keySubscriptionRequest;
        _identityClient->SubscribeToCreateKeysAndCertificateAccepted(
            keySubscriptionRequest, AWS_MQTT_QOS_AT_LEAST_ONCE, onKeysAccepted, onKeysAcceptedSubAck);

        _identityClient->SubscribeToCreateKeysAndCertificateRejected(
            keySubscriptionRequest, AWS_MQTT_QOS_AT_LEAST_ONCE, onKeysRejected, onKeysRejectedSubAck);

        std::cout << "[provision-plugin] Publishing to CreateKeysAndCertificate topic" << std::endl;
        Aws::Iotidentity::CreateKeysAndCertificateRequest createKeysAndCertificateRequest;
        _identityClient->PublishCreateKeysAndCertificate(
            createKeysAndCertificateRequest, AWS_MQTT_QOS_AT_LEAST_ONCE, onKeysPublishSubAck);

        keysPublishCompletedPromise.get_future().wait();
        keysAcceptedCompletedPromise.get_future().wait();
        keysRejectedCompletedPromise.get_future().wait();
    }
    else {
        std::promise<void> csrPublishCompletedPromise;
        std::promise<void> csrAcceptedCompletedPromise;
        std::promise<void> csrRejectedCompletedPromise;

        Aws::Crt::String token;

        auto onCsrPublishSubAck = [&](int ioErr) {
            if (ioErr != AWS_OP_SUCCESS)
            {
                std::cerr<<"[provision-plugin] Error publishing to CreateCertificateFromCsr: %s\n"<<Aws::Crt::ErrorDebugString(ioErr);
                return;
            }

            csrPublishCompletedPromise.set_value();
        };

        auto onCsrAcceptedSubAck = [&](int ioErr) {
            if (ioErr != AWS_OP_SUCCESS)
            {
                std::cerr<<"[provision-plugin] Error subscribing to CreateCertificateFromCsr accepted: %s\n"<<Aws::Crt::ErrorDebugString(ioErr);
                return;
            }

            csrAcceptedCompletedPromise.set_value();
        };

        auto onCsrRejectedSubAck = [&](int ioErr) {
            if (ioErr != AWS_OP_SUCCESS)
            {
                std::cerr<<"[provision-plugin] Error subscribing to CreateCertificateFromCsr rejected: %s\n"<<Aws::Crt::ErrorDebugString(ioErr);
                return;
            }
            csrRejectedCompletedPromise.set_value();
        };

        auto onCsrAccepted = [&](Aws::Iotidentity::CreateCertificateFromCsrResponse *response, int ioErr) {
            if (ioErr == AWS_OP_SUCCESS)
            {
                try {
                    std::filesystem::path rootPath;
                    // Write certificate to the root path
                    std::filesystem::path certPath = rootPath / DEVICE_CERTIFICATE_PATH_RELATIVE_TO_ROOT;

                    std::ofstream outStream(certPath);

                    if (outStream.is_open()) {
                        outStream << response->CertificatePem->c_str();
                    }
                    // Copy private key to the root path
                    std::filesystem::path desiredPath = rootPath / PRIVATE_KEY_PATH_RELATIVE_TO_ROOT;
                    std::filesystem::copy(_deviceConfig.claimKeyPath, desiredPath, std::filesystem::copy_options::overwrite_existing);
                }
                catch (const std::exception &e) {
                    std::cerr<<"[provision-plugin] Error while writing certificate and copying key to root path %s.\n"<<e.what();
                }

                std::cout<<"[provision-plugin] CreateCertificateFromCsrResponse certificateId: %s.\n"<<response->CertificateId->c_str();
                token = *response->CertificateOwnershipToken;
            }
            else
            {
                std::cerr<<"[provision-plugin] Error on subscription: %s.\n"<<Aws::Crt::ErrorDebugString(ioErr);
                return;
            }
        };

        auto onCsrRejected = [&](Aws::Iotidentity::ErrorResponse *error, int ioErr) {
            if (ioErr == AWS_OP_SUCCESS)
            {
                std::cout<<"[provision-plugin] CreateCertificateFromCsr failed with statusCode %d, errorMessage %s and errorCode %s."
                          <<*error->StatusCode<<error->ErrorMessage->c_str()<<error->ErrorCode->c_str();
                return;
            }
            else
            {
                std::cerr<<"[provision-plugin] Error on subscription: %s.\n"<<Aws::Crt::ErrorDebugString(ioErr);
                return;
            }
        };

        // CreateCertificateFromCsr workflow
        std::cout << "[provision-plugin] Subscribing to CreateCertificateFromCsr Accepted and Rejected topics" << std::endl;
        Aws::Iotidentity::CreateCertificateFromCsrSubscriptionRequest csrSubscriptionRequest;
        _identityClient->SubscribeToCreateCertificateFromCsrAccepted(
            csrSubscriptionRequest, AWS_MQTT_QOS_AT_LEAST_ONCE, onCsrAccepted, onCsrAcceptedSubAck);

        _identityClient->SubscribeToCreateCertificateFromCsrRejected(
            csrSubscriptionRequest, AWS_MQTT_QOS_AT_LEAST_ONCE, onCsrRejected, onCsrRejectedSubAck);

        std::cout << "[provision-plugin] Publishing to CreateCertificateFromCsr topic" << std::endl;
        Aws::Iotidentity::CreateCertificateFromCsrRequest createCertificateFromCsrRequest;
        createCertificateFromCsrRequest.CertificateSigningRequest = _deviceConfig.csrPath.value();
        _identityClient->PublishCreateCertificateFromCsr(
            createCertificateFromCsrRequest, AWS_MQTT_QOS_AT_LEAST_ONCE, onCsrPublishSubAck);

        csrPublishCompletedPromise.get_future().wait();
        csrAcceptedCompletedPromise.get_future().wait();
        csrRejectedCompletedPromise.get_future().wait();
    }

    std::this_thread::sleep_for(1s);

    if (_deviceConfig.templateParams.has_value()) {
        registerThing();
    }
}

/**
 * Register the device with AWS IoT
 */
void ProvisionPlugin::registerThing() {
    std::promise<void> registerPublishCompletedPromise;
    std::promise<void> registerAcceptedCompletedPromise;
    std::promise<void> registerRejectedCompletedPromise;

    Aws::Crt::String token;

    auto onRegisterAcceptedSubAck = [&](int ioErr) {
        if (ioErr != AWS_OP_SUCCESS)
        {
            std::cerr<<"[provision-plugin] Error subscribing to RegisterThing accepted: %s\n"<<Aws::Crt::ErrorDebugString(ioErr);
            return;
        }

        registerAcceptedCompletedPromise.set_value();
    };

    auto onRegisterRejectedSubAck = [&](int ioErr) {
        if (ioErr != AWS_OP_SUCCESS)
        {
            std::cerr<<"[provision-plugin] Error subscribing to RegisterThing rejected: %s\n"<<Aws::Crt::ErrorDebugString(ioErr);
            return;
        }
        registerRejectedCompletedPromise.set_value();
    };

    auto onRegisterAccepted = [&](Aws::Iotidentity::RegisterThingResponse *response, int ioErr) {
        if (ioErr == AWS_OP_SUCCESS)
        {
            _thingName = response->ThingName->c_str();
        }
        else
        {
            std::cerr<<"[provision-plugin] Error on subscription: %s.\n"<<Aws::Crt::ErrorDebugString(ioErr);
            return;
        }
    };

    auto onRegisterRejected = [&](Aws::Iotidentity::ErrorResponse *error, int ioErr) {
        if (ioErr == AWS_OP_SUCCESS)
        {
            std::cout<<"[provision-plugin] RegisterThing failed with statusCode %d, errorMessage %s and errorCode %s."
                    <<*error->StatusCode<<error->ErrorMessage->c_str()<<error->ErrorCode->c_str();
        }
        else
        {
            std::cerr<<"[provision-plugin] Error on subscription: %s.\n"<<Aws::Crt::ErrorDebugString(ioErr);
            return;
        }
    };

    auto onRegisterPublishSubAck = [&](int ioErr) {
        if (ioErr != AWS_OP_SUCCESS)
        {
            std::cerr<<"[provision-plugin] Error publishing to RegisterThing: %s\n"<<Aws::Crt::ErrorDebugString(ioErr);
            return;
        }

        registerPublishCompletedPromise.set_value();
    };

    std::cout << "[provision-plugin] Subscribing to RegisterThing Accepted and Rejected topics" << std::endl;
    Aws::Iotidentity::RegisterThingSubscriptionRequest registerSubscriptionRequest;
    registerSubscriptionRequest.TemplateName = _deviceConfig.templateName;

    _identityClient->SubscribeToRegisterThingAccepted(
        registerSubscriptionRequest, AWS_MQTT_QOS_AT_LEAST_ONCE, onRegisterAccepted, onRegisterAcceptedSubAck);

    _identityClient->SubscribeToRegisterThingRejected(
        registerSubscriptionRequest, AWS_MQTT_QOS_AT_LEAST_ONCE, onRegisterRejected, onRegisterRejectedSubAck);

    std::this_thread::sleep_for(2s);

    std::cout << "[provision-plugin] Publishing to RegisterThing topic" << std::endl;
    Aws::Iotidentity::RegisterThingRequest registerThingRequest;
    registerThingRequest.TemplateName = _deviceConfig.templateName;


    const Aws::Crt::String jsonValue = _deviceConfig.templateParams.value();
    Aws::Crt::JsonObject value(jsonValue);
    Aws::Crt::Map<Aws::Crt::String, Aws::Crt::JsonView> pm = value.View().GetAllObjects();
    Aws::Crt::Map<Aws::Crt::String, Aws::Crt::String> params =
        Aws::Crt::Map<Aws::Crt::String, Aws::Crt::String>();

    for (const auto &x : pm)
    {
        params.emplace(x.first, x.second.AsString());
    }

    registerThingRequest.Parameters = params;
    registerThingRequest.CertificateOwnershipToken = token;

    _identityClient->PublishRegisterThing(
        registerThingRequest, AWS_MQTT_QOS_AT_LEAST_ONCE, onRegisterPublishSubAck);

    std::this_thread::sleep_for(2s);

    registerPublishCompletedPromise.get_future().wait();
    registerAcceptedCompletedPromise.get_future().wait();
    registerRejectedCompletedPromise.get_future().wait();
}
