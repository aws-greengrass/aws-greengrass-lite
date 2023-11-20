#pragma once

#include <utility>
#include <filesystem>
#include <fstream>

#include <plugin.hpp>

#include <aws/crt/Api.h>
#include <aws/crt/UUID.h>
#include <aws/crt/http/HttpConnection.h>
#include <aws/crt/io/Bootstrap.h>
#include <aws/crt/io/EventLoopGroup.h>
#include <aws/crt/io/HostResolver.h>
#include <aws/crt/io/TlsOptions.h>
#include <aws/crt/http/HttpProxyStrategy.h>
#include <aws/crt/http/HttpRequestResponse.h>
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

static inline const std::string DEFAULT_AWS_REGION{"us-west-2"};
static inline const std::string DEFAULT_THING_NAME{"GGLiteTest"};
static inline const std::string DEFAULT_POLICY_NAME{"GreengrassIoTThingPolicy"};
static inline const std::string DEFAULT_GROUP_NAME{"GreengrassGroup"};


struct DeviceConfig {
    // TODO: Optional for semantic meaning doesnt effect
    Aws::Crt::String templateName;
    Aws::Crt::String claimCertPath;
    Aws::Crt::String claimKeyPath;
    Aws::Crt::String rootCaPath;
    Aws::Crt::String endpoint;
    Aws::Crt::String rootPath;
    Aws::Crt::Optional<uint64_t> mqttPort;
    Aws::Crt::Optional<Aws::Crt::String> credEndpoint;
    Aws::Crt::Optional<uint64_t> proxyPort;
    Aws::Crt::Optional<Aws::Crt::String> csrPath;
    Aws::Crt::Optional<Aws::Crt::String> deviceId;
    Aws::Crt::Optional<Aws::Crt::String> templateParams; // TODO: Make it required?
    Aws::Crt::Optional<Aws::Crt::String> awsRegion;
    Aws::Crt::Optional<Aws::Crt::String> proxyUrl;
    Aws::Crt::Optional<Aws::Crt::String> proxyUsername;
    Aws::Crt::Optional<Aws::Crt::String> proxyPassword;
};

class ProvisionPlugin: public ggapi::Plugin {
        struct DeviceConfig _deviceConfig;
//        std::unordered_multimap<TopicFilter, ggapi::StringOrd, TopicFilter::Hash> _subscriptions;
//        std::shared_mutex _subscriptionMutex;
        std::shared_ptr<Aws::Crt::Mqtt5::Mqtt5Client> _mqttClient;
        std::shared_ptr<Aws::Iotidentity::IotIdentityClient> _identityClient;

        Aws::Crt::String _thingName;
        std::filesystem::path _certPath;
        std::filesystem::path _keyPath;

//        static const Keys keys;
        static inline std::string DEVICE_CERTIFICATE_PATH_RELATIVE_TO_ROOT = "thingCert.crt";
        static inline std::string PRIVATE_KEY_PATH_RELATIVE_TO_ROOT = "privateKey.key";
//        static ggapi::Struct publishHandler(ggapi::Task, ggapi::StringOrd, ggapi::Struct args);
//        static ggapi::Struct publishHandlerImpl(ggapi::Struct args);
//        static ggapi::Struct subscribeHandler(ggapi::Task, ggapi::StringOrd, ggapi::Struct args);
//        ggapi::Struct subscribeHandlerImpl(ggapi::Struct args);

    public:
        ProvisionPlugin() = default;
        ~ProvisionPlugin() override = default;
        void beforeLifecycle(ggapi::StringOrd phase, ggapi::Struct data) override;
        bool onStart(ggapi::Struct data) override;
        bool onRun(ggapi::Struct data) override;
        static ggapi::Struct testListener(ggapi::Task task, ggapi::StringOrd topic, ggapi::Struct callData);
        static ProvisionPlugin &get() {
            static ProvisionPlugin instance{};
            return instance;
        }

        void generateCredentials();

        void registerThing();

        bool initMqtt();

        void setDeviceConfig(const DeviceConfig &);

        ggapi::Struct provisionDevice();
};
