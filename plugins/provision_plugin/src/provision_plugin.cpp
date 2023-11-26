#include "provision_plugin.hpp"
#include <chrono>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;

// Global initialization of the API
static const Aws::Crt::ApiHandle apiHandle{};
const Keys ProvisionPlugin::keys{};

/**
 *
 * @param phase
 * @param data
 */
void ProvisionPlugin::beforeLifecycle(ggapi::StringOrd phase, ggapi::Struct data) {
    std::cout << "[provision-plugin] Running lifecycle provision plugin... "
              << ggapi::StringOrd{phase}.toString() << std::endl;
}

/**
 * Listen on the well-known Provisioning topic, and if a request for provisioning comes in,
 * perform a By-Claim provisioning action to IoT Core.
 */
ggapi::Struct ProvisionPlugin::brokerListener(
    ggapi::Task, ggapi::StringOrd, ggapi::Struct) {
    ProvisionPlugin& pluginInstance = ProvisionPlugin::get();
    pluginInstance.setDeviceConfig();
    return pluginInstance.provisionDevice();
}

bool ProvisionPlugin::onBootstrap(ggapi::Struct data) {
    data.put(NAME, keys.serviceName);
    return true;
}

/**
 * This cycle is normally used for binding. Provisioning may be called very early on, so
 * bind the provisioning topic during this binding phase. (Atypical)
 */
bool ProvisionPlugin::onBind(ggapi::Struct data) {
    _subscription = getScope().subscribeToTopic(keys.topicName, brokerListener);
    return true;
}

/**
 *
 * @param data
 * @return True if successful, false otherwise.
 */
bool ProvisionPlugin::onStart(ggapi::Struct data) {
    return true;
}

/**
 *
 * @param data
 * @return True if successful, false otherwise.
 */
bool ProvisionPlugin::onRun(ggapi::Struct data) {
    return true;
}

/**
 * Release subscriptions during termination.
 */
bool ProvisionPlugin::onTerminate(ggapi::Struct data) {
    _subscription.load().release();
    return true;
}

/**
 * Provision device with csr or create key and certificate with certificate authority
 * @return Response containing provisioned thing information
 */
ggapi::Struct ProvisionPlugin::provisionDevice() {
    if(initMqtt()) {
        try {
            generateCredentials();
            ggapi::Struct response = ggapi::Struct::create();
            response.put("thingName", _thingName);
            response.put("keyPath", _keyPath.string());
            response.put("certPath", _certPath.string());
            return response;
        } catch(const std::exception &e) {
            std::cerr << "[provision-plugin] Error while provisioning the device\n";
            throw e;
        }
    } else {
        throw std::runtime_error("[provision-plugin] Unable to initialize the mqtt client\n");
    }
}

/**
 * Set the device configuration for provisioning
 * @param deviceConfig Device configuration to be copied
 */
void ProvisionPlugin::setDeviceConfig() {
    auto serviceConfig = getConfig().getValue<ggapi::Struct>({"configuration"});

    _deviceConfig.rootPath = serviceConfig.getValue<std::string>({"rootPath"});
    _deviceConfig.rootCaPath = serviceConfig.getValue<std::string>({"rootCaPath"});
    _deviceConfig.templateName = serviceConfig.getValue<std::string>({"templateName"});
    _deviceConfig.claimKeyPath = serviceConfig.getValue<std::string>({"claimKeyPath"});
    _deviceConfig.claimCertPath = serviceConfig.getValue<std::string>({"claimCertPath"});
    _deviceConfig.endpoint = serviceConfig.getValue<std::string>({"iotDataEndpoint"});
    _deviceConfig.templateParams = serviceConfig.getValue<std::string>({"templateParams"});
    _deviceConfig.proxyUsername = serviceConfig.getValue<std::string>({"proxyUsername"});
    _deviceConfig.proxyPassword = serviceConfig.getValue<std::string>({"proxyPassword"});
    _deviceConfig.mqttPort = serviceConfig.getValue<uint64_t>({"mqttPort"});
    _deviceConfig.proxyUrl = serviceConfig.getValue<std::string>({"proxyUrl"});
    _deviceConfig.csrPath = serviceConfig.getValue<std::string>({"csrPath"});
    _deviceConfig.deviceId  = serviceConfig.getValue<std::string>({"deviceId"});

    if(_deviceConfig.templateName.empty()) {
        throw std::runtime_error("Template name not found.");
    }
    if((_deviceConfig.claimCertPath.empty() || _deviceConfig.claimKeyPath.empty())
       && _deviceConfig.rootCaPath.empty()) {
        throw std::runtime_error(
            "Not enough information to provision the device, check the configuration.");
    }
    if(_deviceConfig.rootPath.empty()) {
        throw std::runtime_error("Root path not found.");
    }
    if (_deviceConfig.deviceId.empty()) {
        _deviceConfig.deviceId = Aws::Crt::String("temp-") + Aws::Crt::UUID().ToString();
    }
    _keyPath = std::filesystem::path(_deviceConfig.rootPath) / PRIVATE_KEY_PATH_RELATIVE_TO_ROOT;
    _certPath =
        std::filesystem::path(_deviceConfig.rootPath) / DEVICE_CERTIFICATE_PATH_RELATIVE_TO_ROOT;
}

/**
 * Initialize the Mqtt client
 * @return True if successful, false otherwise.
 */
bool ProvisionPlugin::initMqtt() {
    std::promise<bool> connectionPromise;
    std::promise<void> disconnectPromise;

    std::unique_ptr<Aws::Iot::Mqtt5ClientBuilder> builder{
        Aws::Iot::Mqtt5ClientBuilder::NewMqtt5ClientBuilderWithMtlsFromPath(
            _deviceConfig.endpoint,
            _deviceConfig.claimCertPath.c_str(),
            _deviceConfig.claimKeyPath.c_str())};

    if(!builder) {
        std::cerr << "Failed to setup MQTT client builder " << Aws::Crt::LastError() << ": "
                  << Aws::Crt::ErrorDebugString(Aws::Crt::LastError()) << std::endl;
        return false;
    }

    builder->WithCertificateAuthority(_deviceConfig.rootCaPath.c_str());

    // TODO: Custom mqtt port
    //    if (_deviceConfig.mqttPort != -1) {
    //        builder->WithPort(static_cast<uint16_t>(_deviceConfig.mqttPort));
    //    }
    //    else {
    //        builder->WithPort(80);
    //    }

    // http proxy
    if(!(_deviceConfig.proxyUrl.empty() || _deviceConfig.proxyUsername.empty()
         || _deviceConfig.proxyPassword.empty())) {
        struct aws_allocator *allocator = aws_default_allocator();
        Aws::Crt::Http::HttpClientConnectionProxyOptions proxyOptions;
        proxyOptions.HostName = getHostFromProxyUrl(_deviceConfig.proxyUrl);
        proxyOptions.Port = getPortFromProxyUrl(_deviceConfig.proxyUrl);
        proxyOptions.ProxyConnectionType = Aws::Crt::Http::AwsHttpProxyConnectionType::Tunneling;

        Aws::Crt::Io::TlsConnectionOptions tlsConnectionOptions;
        Aws::Crt::Io::TlsContextOptions proxyTlsCtxOptions =
            Aws::Crt::Io::TlsContextOptions::InitDefaultClient();
        proxyTlsCtxOptions.SetVerifyPeer(false);

        std::shared_ptr<Aws::Crt::Io::TlsContext> proxyTlsContext =
            Aws::Crt::MakeShared<Aws::Crt::Io::TlsContext>(
                allocator, proxyTlsCtxOptions, Aws::Crt::Io::TlsMode::CLIENT, allocator);
        tlsConnectionOptions = proxyTlsContext->NewConnectionOptions();
        Aws::Crt::ByteCursor proxyName = ByteCursorFromString(proxyOptions.HostName);
        tlsConnectionOptions.SetServerName(proxyName);

        proxyOptions.TlsOptions = tlsConnectionOptions;

        proxyOptions.AuthType = Aws::Crt::Http::AwsHttpProxyAuthenticationType::Basic;
        proxyOptions.BasicAuthUsername = _deviceConfig.proxyUsername;
        proxyOptions.BasicAuthPassword = _deviceConfig.proxyPassword;
        builder->WithHttpProxyOptions(proxyOptions);
    }

    // connection options
    auto connectOptions = std::make_shared<Aws::Crt::Mqtt5::ConnectPacket>();
    connectOptions->WithClientId(_deviceConfig.deviceId);
    builder->WithConnectOptions(connectOptions);

    // register callbacks
    builder->WithClientConnectionSuccessCallback(
        [&connectionPromise](const Aws::Crt::Mqtt5::OnConnectionSuccessEventData &eventData) {
            std::cerr << "[provision-plugin] Connection successful with clientid "
                      << eventData.negotiatedSettings->getClientId() << "." << std::endl;
            connectionPromise.set_value(true);
        });

    builder->WithClientConnectionFailureCallback(
        [&connectionPromise](const Aws::Crt::Mqtt5::OnConnectionFailureEventData &eventData) {
            std::cerr << "[provision-plugin] Connection failed: "
                      << aws_error_debug_str(eventData.errorCode) << "." << std::endl;
            connectionPromise.set_value(false);
        });

    builder->WithClientAttemptingConnectCallback(
        [](const Aws::Crt::Mqtt5::OnAttemptingConnectEventData &) {
            std::cout << "[provision-plugin] Attempting to connect...\n";
        });

    builder->WithClientDisconnectionCallback(
        [&disconnectPromise](const Aws::Crt::Mqtt5::OnDisconnectionEventData &eventData) {
            std::cout << "[provision-plugin] Mqtt client disconnected\n"
                      << aws_error_debug_str(eventData.errorCode);
            disconnectPromise.set_value();
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

    if(_deviceConfig.csrPath.empty()) {
        std::promise<void> keysPublishCompletedPromise;
        std::promise<void> keysAcceptedCompletedPromise;
        std::promise<void> keysRejectedCompletedPromise;

        auto onKeysPublishSubAck = [&](int ioErr) {
            if(ioErr != AWS_OP_SUCCESS) {
                std::cerr << "[provision-plugin] Error publishing to CreateKeysAndCertificate: "
                          << Aws::Crt::ErrorDebugString(ioErr) << std::endl;
                return;
            }

            keysPublishCompletedPromise.set_value();
        };

        auto onKeysAcceptedSubAck = [&](int ioErr) {
            if(ioErr != AWS_OP_SUCCESS) {
                std::cerr
                    << "[provision-plugin] Error subscribing to CreateKeysAndCertificate accepted: "
                    << Aws::Crt::ErrorDebugString(ioErr) << std::endl;
                return;
            }

            keysAcceptedCompletedPromise.set_value();
        };

        auto onKeysRejectedSubAck = [&](int ioErr) {
            if(ioErr != AWS_OP_SUCCESS) {
                std::cerr
                    << "[provision-plugin] Error subscribing to CreateKeysAndCertificate rejected: "
                    << Aws::Crt::ErrorDebugString(ioErr) << std::endl;
                return;
            }
            keysRejectedCompletedPromise.set_value();
        };

        auto onKeysAccepted = [this](
                                  Aws::Iotidentity::CreateKeysAndCertificateResponse *response,
                                  int ioErr) {
            if(ioErr == AWS_OP_SUCCESS) {
                try {
                    std::filesystem::path rootPath = _deviceConfig.rootPath;

                    // Write key and certificate to the root path
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

                    _token = *response->CertificateOwnershipToken;
                } catch(std::exception &e) {
                    std::cerr << "[provision-plugin] Error while writing keys and certificate to "
                                 "root path: "
                              << e.what() << std::endl;
                }
            } else {
                std::cerr << "[provision-plugin] Error on subscription: "
                          << Aws::Crt::ErrorDebugString(ioErr) << std::endl;
                return;
            }
        };

        auto onKeysRejected = [&](Aws::Iotidentity::ErrorResponse *error, int ioErr) {
            if(ioErr == AWS_OP_SUCCESS) {
                std::cout << "[provision-plugin] CreateKeysAndCertificate failed with statusCode "
                          << *error->StatusCode << ", errorMessage " << error->ErrorMessage->c_str()
                          << " and errorCode " << error->ErrorCode->c_str();
                return;
            } else {
                std::cerr << "[provision-plugin] Error on subscription: "
                          << Aws::Crt::ErrorDebugString(ioErr) << std::endl;
                return;
            }
        };

        std::cout << "[provision-plugin] Subscribing to CreateKeysAndCertificate Accepted and "
                     "Rejected topics"
                  << std::endl;
        Aws::Iotidentity::CreateKeysAndCertificateSubscriptionRequest keySubscriptionRequest;
        _identityClient->SubscribeToCreateKeysAndCertificateAccepted(
            keySubscriptionRequest,
            AWS_MQTT_QOS_AT_LEAST_ONCE,
            onKeysAccepted,
            onKeysAcceptedSubAck);

        _identityClient->SubscribeToCreateKeysAndCertificateRejected(
            keySubscriptionRequest,
            AWS_MQTT_QOS_AT_LEAST_ONCE,
            onKeysRejected,
            onKeysRejectedSubAck);

        std::cout << "[provision-plugin] Publishing to CreateKeysAndCertificate topic" << std::endl;
        Aws::Iotidentity::CreateKeysAndCertificateRequest createKeysAndCertificateRequest;
        _identityClient->PublishCreateKeysAndCertificate(
            createKeysAndCertificateRequest, AWS_MQTT_QOS_AT_LEAST_ONCE, onKeysPublishSubAck);

        keysPublishCompletedPromise.get_future().wait();
        keysAcceptedCompletedPromise.get_future().wait();
        keysRejectedCompletedPromise.get_future().wait();
    } else {
        std::promise<void> csrPublishCompletedPromise;
        std::promise<void> csrAcceptedCompletedPromise;
        std::promise<void> csrRejectedCompletedPromise;

        auto onCsrPublishSubAck = [&](int ioErr) {
            if(ioErr != AWS_OP_SUCCESS) {
                std::cerr << "[provision-plugin] Error publishing to CreateCertificateFromCsr: "
                          << Aws::Crt::ErrorDebugString(ioErr) << std::endl;
                return;
            }

            csrPublishCompletedPromise.set_value();
        };

        auto onCsrAcceptedSubAck = [&](int ioErr) {
            if(ioErr != AWS_OP_SUCCESS) {
                std::cerr
                    << "[provision-plugin] Error subscribing to CreateCertificateFromCsr accepted: "
                    << Aws::Crt::ErrorDebugString(ioErr) << std::endl;
                return;
            }

            csrAcceptedCompletedPromise.set_value();
        };

        auto onCsrRejectedSubAck = [&](int ioErr) {
            if(ioErr != AWS_OP_SUCCESS) {
                std::cerr
                    << "[provision-plugin] Error subscribing to CreateCertificateFromCsr rejected: "
                    << Aws::Crt::ErrorDebugString(ioErr) << std::endl;
                return;
            }
            csrRejectedCompletedPromise.set_value();
        };

        auto onCsrAccepted = [this](
                                 Aws::Iotidentity::CreateCertificateFromCsrResponse *response,
                                 int ioErr) {
            if(ioErr == AWS_OP_SUCCESS) {
                try {
                    std::filesystem::path rootPath;
                    // Write certificate to the root path
                    std::filesystem::path certPath =
                        rootPath / DEVICE_CERTIFICATE_PATH_RELATIVE_TO_ROOT;

                    std::ofstream outStream(certPath);

                    if(outStream.is_open()) {
                        outStream << response->CertificatePem->c_str();
                    }
                    // Copy private key to the root path
                    std::filesystem::path desiredPath =
                        rootPath / PRIVATE_KEY_PATH_RELATIVE_TO_ROOT;
                    std::filesystem::copy(
                        _deviceConfig.claimKeyPath,
                        desiredPath,
                        std::filesystem::copy_options::overwrite_existing);
                } catch(const std::exception &e) {
                    std::cerr << "[provision-plugin] Error while writing certificate and copying "
                                 "key to root path "
                              << e.what() << std::endl;
                }

                std::cout << "[provision-plugin] CreateCertificateFromCsrResponse certificateId: "
                          << response->CertificateId->c_str() << std::endl;
                _token = *response->CertificateOwnershipToken;
            } else {
                std::cerr << "[provision-plugin] Error on subscription: "
                          << Aws::Crt::ErrorDebugString(ioErr) << std::endl;
                return;
            }
        };

        auto onCsrRejected = [&](Aws::Iotidentity::ErrorResponse *error, int ioErr) {
            if(ioErr == AWS_OP_SUCCESS) {
                std::cout << "[provision-plugin] CreateCertificateFromCsr failed with statusCode "
                          << *error->StatusCode << ", errorMessage " << error->ErrorMessage->c_str()
                          << " and errorCode " << error->ErrorCode->c_str();
                return;
            } else {
                std::cerr << "[provision-plugin] Error on subscription: "
                          << Aws::Crt::ErrorDebugString(ioErr) << std::endl;
                return;
            }
        };

        // CreateCertificateFromCsr workflow
        std::cout << "[provision-plugin] Subscribing to CreateCertificateFromCsr Accepted and "
                     "Rejected topics"
                  << std::endl;
        Aws::Iotidentity::CreateCertificateFromCsrSubscriptionRequest csrSubscriptionRequest;
        _identityClient->SubscribeToCreateCertificateFromCsrAccepted(
            csrSubscriptionRequest, AWS_MQTT_QOS_AT_LEAST_ONCE, onCsrAccepted, onCsrAcceptedSubAck);

        _identityClient->SubscribeToCreateCertificateFromCsrRejected(
            csrSubscriptionRequest, AWS_MQTT_QOS_AT_LEAST_ONCE, onCsrRejected, onCsrRejectedSubAck);

        std::cout << "[provision-plugin] Publishing to CreateCertificateFromCsr topic" << std::endl;
        Aws::Iotidentity::CreateCertificateFromCsrRequest createCertificateFromCsrRequest;
        createCertificateFromCsrRequest.CertificateSigningRequest = _deviceConfig.csrPath;
        _identityClient->PublishCreateCertificateFromCsr(
            createCertificateFromCsrRequest, AWS_MQTT_QOS_AT_LEAST_ONCE, onCsrPublishSubAck);

        csrPublishCompletedPromise.get_future().wait();
        csrAcceptedCompletedPromise.get_future().wait();
        csrRejectedCompletedPromise.get_future().wait();
    }

    // TODO: Comment needs to explain why we need to sleep for 1s here, it seems so arbitrary
    // Also, use the new API instead to schedule registerThing() after 1 second
    std::this_thread::sleep_for(1s);

    registerThing();
}

/**
 * Get port from a valid url
 * @param proxyUrl A proxy url to get the port from
 * @return Port of the proxy url
 */
uint64_t ProvisionPlugin::getPortFromProxyUrl(const Aws::Crt::String &proxyUrl) {
    size_t first = proxyUrl.find_first_of(':');
    size_t last = proxyUrl.find_last_of(':');
    std::string proxyString{proxyUrl};
    if(first != last) {
        return std::stoi(proxyString.substr(last + 1));
    } else {
        std::string protocol = proxyString.substr(0, last);
        if(protocol == "https") {
            return HTTPS_PORT;
        } else if(protocol == "http") {
            return HTTP_PORT;
        } else if(protocol == "socks5") {
            return SOCKS5_PORT;
        } else {
            return -1;
        }
    }
}

/**
 * Get hostname from a valid url
 * @param proxyUrl A proxy url to get the hostname from
 * @return Hostname of the proxy url
 */
Aws::Crt::String ProvisionPlugin::getHostFromProxyUrl(const Aws::Crt::String &proxyUrl) {
    auto first = proxyUrl.find_first_of(':');
    auto last = proxyUrl.find_first_of(':', first + 1);
    first += 3;
    return proxyUrl.substr(first, last - first);
}

/**
 * Register the device with AWS IoT
 */
void ProvisionPlugin::registerThing() {
    std::promise<void> registerPublishCompletedPromise;
    std::promise<void> registerAcceptedCompletedPromise;
    std::promise<void> registerRejectedCompletedPromise;

    auto onRegisterAcceptedSubAck = [&](int ioErr) {
        if(ioErr != AWS_OP_SUCCESS) {
            std::cerr << "[provision-plugin] Error subscribing to RegisterThing accepted: "
                      << Aws::Crt::ErrorDebugString(ioErr) << std::endl;
            return;
        }

        registerAcceptedCompletedPromise.set_value();
    };

    auto onRegisterRejectedSubAck = [&](int ioErr) {
        if(ioErr != AWS_OP_SUCCESS) {
            std::cerr << "[provision-plugin] Error subscribing to RegisterThing rejected: "
                      << Aws::Crt::ErrorDebugString(ioErr) << std::endl;
            return;
        }
        registerRejectedCompletedPromise.set_value();
    };

    auto onRegisterAccepted = [this](Aws::Iotidentity::RegisterThingResponse *response, int ioErr) {
        if(ioErr == AWS_OP_SUCCESS) {
            _thingName = response->ThingName->c_str(); // NOLINT(*-redundant-string-cstr)
        } else {
            std::cerr << "[provision-plugin] Error on subscription: "
                      << Aws::Crt::ErrorDebugString(ioErr) << std::endl;
            return;
        }
    };

    auto onRegisterRejected = [&](Aws::Iotidentity::ErrorResponse *error, int ioErr) {
        if(ioErr == AWS_OP_SUCCESS) {
            std::cout << "[provision-plugin] RegisterThing failed with statusCode "
                      << *error->StatusCode << ", errorMessage " << error->ErrorMessage->c_str()
                      << " and errorCode " << error->ErrorCode->c_str() << std::endl;
        } else {
            std::cerr << "[provision-plugin] Error on subscription: "
                      << Aws::Crt::ErrorDebugString(ioErr) << std::endl;
            return;
        }
    };

    auto onRegisterPublishSubAck = [&](int ioErr) {
        if(ioErr != AWS_OP_SUCCESS) {
            std::cerr << "[provision-plugin] Error publishing to RegisterThing: "
                      << Aws::Crt::ErrorDebugString(ioErr) << std::endl;
            return;
        }

        registerPublishCompletedPromise.set_value();
    };

    std::cout << "[provision-plugin] Subscribing to RegisterThing Accepted and Rejected topics"
              << std::endl;
    Aws::Iotidentity::RegisterThingSubscriptionRequest registerSubscriptionRequest;
    registerSubscriptionRequest.TemplateName = _deviceConfig.templateName;

    _identityClient->SubscribeToRegisterThingAccepted(
        registerSubscriptionRequest,
        AWS_MQTT_QOS_AT_LEAST_ONCE,
        onRegisterAccepted,
        onRegisterAcceptedSubAck);

    _identityClient->SubscribeToRegisterThingRejected(
        registerSubscriptionRequest,
        AWS_MQTT_QOS_AT_LEAST_ONCE,
        onRegisterRejected,
        onRegisterRejectedSubAck);

    std::this_thread::sleep_for(1s);

    std::cout << "[provision-plugin] Publishing to RegisterThing topic" << std::endl;
    Aws::Iotidentity::RegisterThingRequest registerThingRequest;
    registerThingRequest.TemplateName = _deviceConfig.templateName;

    const Aws::Crt::String jsonValue = _deviceConfig.templateParams;
    Aws::Crt::JsonObject value(jsonValue);
    Aws::Crt::Map<Aws::Crt::String, Aws::Crt::JsonView> pm = value.View().GetAllObjects();
    Aws::Crt::Map<Aws::Crt::String, Aws::Crt::String> params =
        Aws::Crt::Map<Aws::Crt::String, Aws::Crt::String>();

    for(const auto &x : pm) {
        params.emplace(x.first, x.second.AsString());
    }

    registerThingRequest.Parameters = params;
    registerThingRequest.CertificateOwnershipToken = _token;

    _identityClient->PublishRegisterThing(
        registerThingRequest, AWS_MQTT_QOS_AT_LEAST_ONCE, onRegisterPublishSubAck);

    std::this_thread::sleep_for(1s);

    registerPublishCompletedPromise.get_future().wait();
    registerAcceptedCompletedPromise.get_future().wait();
    registerRejectedCompletedPromise.get_future().wait();
}
