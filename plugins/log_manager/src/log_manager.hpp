#pragma once

#include <plugin.hpp>
#include <shared_device_sdk.hpp>

class LogManager : public ggapi::Plugin {
private:
    struct ThingInfo {
        Aws::Crt::String thingName;
        std::string credEndpoint;
        Aws::Crt::String dataEndpoint;
        std::string certPath;
        std::string keyPath;
        std::string rootCaPath;
        std::string rootPath;
    } _thingInfo;

    struct LogGroup {
        std::string componentType;
        std::string region;
        std::string componentName;
    } _logGroup;

    struct LogStream {
        std::string date;
    } _logStream;

    mutable std::shared_mutex _mutex;
    ggapi::Struct _nucleus;
    ggapi::Struct _system;
    ggapi::Struct _config;
    std::string _iotRoleAlias;
    std::string _savedToken;
    ggapi::Subscription _requestTesSubscription;
    ggapi::Promise _logUploadPromise;

    static constexpr std::string_view ROOT_CA_PATH = "rootCaPath";
    static constexpr std::string_view CERT_PATH = "certificateFilePath";
    static constexpr std::string_view PRIVATE_KEY_PATH = "privateKeyPath";
    static constexpr std::string_view THING_NAME = "thingName";
    static constexpr std::string_view CONFIGURATION_KEY = "configuration";
    static constexpr std::string_view IOT_ROLE_ALIAS = "iotRoleAlias";
    static constexpr std::string_view IOT_CRED_ENDPOINT = "iotCredEndpoint";

    void readDeviceInfo();
    void retrieveCredentialsFromTES();
    void processLogsAndUpload();


public:
    LogManager() = default;
    bool onInitialize(ggapi::Struct data) override;
    bool onStart(ggapi::Struct data) override;
    bool onStop(ggapi::Struct data) override;
    bool onError_stop(ggapi::Struct data) override;

    static LogManager &get() {
        static LogManager instance{};
        return instance;
    }
};
