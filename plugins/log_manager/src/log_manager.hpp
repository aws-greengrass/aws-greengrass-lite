#pragma once

#include <plugin.hpp>
#include <rapidjson/document.h>
#include <shared_device_sdk.hpp>
#include <regex>
#include <filesystem>

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
    ggapi::Subscription _requestTesSubscription;

    static constexpr std::string_view THING_NAME = "thingName";
    static constexpr std::string_view TES_REQUEST_TOPIC = "aws.greengrass.requestTES";
    static constexpr int TIME_OUT_MS = 5000;
    static constexpr int PORT_NUM = 443;
    static constexpr int UPLOAD_FREQUENCY = 300;

    struct Config {
        static constexpr std::string_view LOGS_UPLOADER_PERIODIC_UPDATE_INTERVAL_SEC =
            "periodicUploadIntervalSec";
        static constexpr std::string_view LOGS_UPLOADER_CONFIGURATION_TOPIC =
            "logsUploaderConfiguration";
        static constexpr std::string_view SYSTEM_LOGS_COMPONENT_NAME = "System";
        static constexpr std::string_view DEFAULT_FILE_REGEX = "^%s\\w*.log";
        static constexpr std::string_view COMPONENT_LOGS_CONFIG_TOPIC_NAME =
            "componentLogsConfiguration";
        static constexpr std::string_view COMPONENT_LOGS_CONFIG_MAP_TOPIC_NAME =
            "componentLogsConfigurationMap";
        static constexpr std::string_view SYSTEM_LOGS_CONFIG_TOPIC_NAME =
            "systemLogsConfiguration";
        static constexpr std::string_view COMPONENT_NAME_CONFIG_TOPIC_NAME = "componentName";
        static constexpr std::string_view FILE_REGEX_CONFIG_TOPIC_NAME = "logFileRegex";
        static constexpr std::string_view FILE_DIRECTORY_PATH_CONFIG_TOPIC_NAME =
            "logFileDirectoryPath";
        static constexpr std::string_view MIN_LOG_LEVEL_CONFIG_TOPIC_NAME = "minimumLogLevel";
        static constexpr std::string_view UPLOAD_TO_CW_CONFIG_TOPIC_NAME = "uploadToCloudWatch";
    };

    enum ComponentType {
        GreengrassSystemComponent,
        UserComponent
    };

    struct ComponentLogConfiguration {
        std::regex _fileNameRegex;
        std::filesystem::path _directoryPath;
        std::string _name;
        // std::regex _multiLineStartPattern;
        // Level minimumLogLevel = Level.INFO; --config for log-level
        bool uploadToCloudWatch;
        ComponentType componentType;
    };

    std::unordered_map<std::string, ComponentLogConfiguration> componentLogConfigurations;

    void retrieveCredentialsFromTES();
    void makeHTTPCallToCloudwatchLogs(const std::string& action, const rapidjson::Document& requestBody);
    void processLogsAndUpload();

    void uploadThread(ggapi::Struct data);
    std::thread _upload;

public:
    LogManager() = default;
    void onInitialize(ggapi::Struct data) override;
    void onStart(ggapi::Struct data) override;
    void onStop(ggapi::Struct data) override;

    static LogManager &get() {
        static LogManager instance{};
        return instance;
    }
};
