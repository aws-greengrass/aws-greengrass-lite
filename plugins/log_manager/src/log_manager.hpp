#pragma once

#include <plugin.hpp>
#include <rapidjson/document.h>
#include <shared_device_sdk.hpp>

class LogManager : public ggapi::Plugin {
private:
    struct LogGroup {
        std::string componentType;
        std::string region;
        std::string componentName;
    } _logGroup;

    struct LogStream {
        std::string date;
        Aws::Crt::String thingName;
    } _logStream;

    mutable std::shared_mutex _mutex;
    ggapi::Struct _nucleus;
    ggapi::Struct _system;
    ggapi::Struct _config;
    ggapi::Struct _credentials;
    std::string _iotRoleAlias;
    ggapi::Subscription _requestTesSubscription;
    ggapi::Promise _logUploadPromise;

    static constexpr std::string_view THING_NAME = "thingName";
    static constexpr std::string_view TES_REQUEST_TOPIC = "aws.greengrass.requestTES";
    static constexpr int TIME_OUT_MS = 5000;
    static constexpr int PORT_NUM = 443;
    static constexpr int CHUNK_SIZE = 1024;



    void retrieveCredentialsFromTES();
    void setupClient(Aws::Crt::Io::TlsConnectionOptions tlsConnectionOptions,
                     const std::string &uriAsString,
                     Aws::Crt::Http::HttpRequest &request,
                     Aws::Crt::Http::HttpRequestOptions requestOptions,
                     Aws::Crt::Allocator *allocator, rapidjson::Document requestBody);
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
