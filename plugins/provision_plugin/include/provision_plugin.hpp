#pragma once

#include <filesystem>
#include <fstream>
#include <utility>

#include <plugin.hpp>

#include <aws/crt/Api.h>
#include <aws/crt/UUID.h>
#include <aws/crt/http/HttpConnection.h>
#include <aws/crt/http/HttpProxyStrategy.h>
#include <aws/crt/http/HttpRequestResponse.h>
#include <aws/crt/io/TlsOptions.h>
#include <aws/crt/mqtt/Mqtt5Packets.h>

#include <aws/iot/Mqtt5Client.h>

#include <aws/iotidentity/CreateCertificateFromCsrRequest.h>
#include <aws/iotidentity/CreateCertificateFromCsrResponse.h>
#include <aws/iotidentity/CreateCertificateFromCsrSubscriptionRequest.h>
#include <aws/iotidentity/CreateKeysAndCertificateRequest.h>
#include <aws/iotidentity/CreateKeysAndCertificateResponse.h>
#include <aws/iotidentity/CreateKeysAndCertificateSubscriptionRequest.h>
#include <aws/iotidentity/ErrorResponse.h>
#include <aws/iotidentity/IotIdentityClient.h>
#include <aws/iotidentity/RegisterThingRequest.h>
#include <aws/iotidentity/RegisterThingResponse.h>
#include <aws/iotidentity/RegisterThingSubscriptionRequest.h>

struct Keys {
    ggapi::Symbol topicName{"aws.greengrass.RequestDeviceProvision"};
    ggapi::Symbol serviceName{"aws.greengrass.FleetProvisioningByClaim"};

    static const Keys &get() {
        static std::unique_ptr<Keys> keyRef;
        if(keyRef == nullptr) {
            keyRef = std::make_unique<Keys>();
        }
        return *keyRef;
    }
};

struct DeviceConfig {
    Aws::Crt::String templateName;
    Aws::Crt::String claimCertPath;
    Aws::Crt::String claimKeyPath;
    Aws::Crt::String rootCaPath;
    Aws::Crt::String endpoint;
    Aws::Crt::String rootPath;
    Aws::Crt::String templateParams;
    uint64_t mqttPort = -1;
    Aws::Crt::String csrPath;
    Aws::Crt::String deviceId;
    Aws::Crt::String awsRegion;
    Aws::Crt::String proxyUrl;
    Aws::Crt::String proxyUsername;
    Aws::Crt::String proxyPassword;
};

class ProvisionPlugin : public ggapi::Plugin {
    // TODO - values below are shared across multiple threads and needs to be made thread safe
    struct DeviceConfig _deviceConfig;
    std::atomic<ggapi::Subscription> _subscription;
    std::shared_ptr<Aws::Crt::Mqtt5::Mqtt5Client> _mqttClient;
    std::shared_ptr<Aws::Iotidentity::IotIdentityClient> _identityClient;
    Aws::Crt::String _token;

    Aws::Crt::String _thingName;
    std::filesystem::path _certPath;
    std::filesystem::path _keyPath;

    static const Keys keys;
    static constexpr std::string_view DEVICE_CERTIFICATE_PATH_RELATIVE_TO_ROOT = "thingCert.crt";
    static constexpr std::string_view PRIVATE_KEY_PATH_RELATIVE_TO_ROOT = "privateKey.key";
    static constexpr auto HTTP_PORT = 80;
    static constexpr auto HTTPS_PORT = 443;
    static constexpr auto SOCKS5_PORT = 1080;

public:
    ProvisionPlugin(){ };
    void beforeLifecycle(ggapi::StringOrd phase, ggapi::Struct data) override;
    bool onBootstrap(ggapi::Struct data) override;
    bool onBind(ggapi::Struct data) override;
    bool onStart(ggapi::Struct data) override;
    bool onRun(ggapi::Struct data) override;
    bool onTerminate(ggapi::Struct data) override;
    static ggapi::Struct brokerListener(
        ggapi::Task task, ggapi::StringOrd topic, ggapi::Struct callData);
    static ProvisionPlugin &get() {
        static ProvisionPlugin instance{};
        return instance;
    }

    static uint64_t getPortFromProxyUrl(const Aws::Crt::String &proxyUrl);
    static Aws::Crt::String getHostFromProxyUrl(const Aws::Crt::String &proxyUrl);

    void generateCredentials();

    void registerThing();

    bool initMqtt();

    void setDeviceConfig();

    ggapi::Struct provisionDevice();
};
