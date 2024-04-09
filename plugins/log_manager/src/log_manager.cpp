#include "curl/curl.h"
#include "futures.hpp"
#include "log_manager.hpp"

const auto LOG = ggapi::Logger::of("LogManager");

bool LogManager::onInitialize(ggapi::Struct data) {
    // TODO: retrieve and process system config
    std::unique_lock guard{_mutex};
    _nucleus = data.getValue<ggapi::Struct>({"nucleus"});
    _system = data.getValue<ggapi::Struct>({"system"});
    return true;
}

bool LogManager::onStart(ggapi::Struct data) {
    // TODO: subscribe to LPC topics and register log uploading callback
    retrieveCredentialsFromTES();

    _logUploadPromise = _logUploadPromise.later(300, &LogManager::processLogsAndUpload);
    return true;
}

bool LogManager::onStop(ggapi::Struct data) {
    return true;
}

bool LogManager::onError_stop(ggapi::Struct data) {
    return true;
}

void LogManager::retrieveCredentialsFromTES() {
    auto request{ggapi::Struct::create()};
    request.put("test", "some-unique-token");
    auto tesFuture = ggapi::Subscription::callTopicFirst(
        ggapi::Symbol{TES_REQUEST_TOPIC}, request);
    if(tesFuture) {
        _credentials = ggapi::Struct(tesFuture.waitAndGetValue());
    }
    _credentials = {};
    _credentials.toJson().write(std::cout);
}

// TODO: how to get last log we left off at and continue uploading from there???
void LogManager::processLogsAndUpload() {
    CURL* curl = curl_easy_init();
    if (curl) {
        // construct request body

        // TODO: remove hardcoded values
        auto system = _system;
        _logGroup.componentType = "GreengrassSystemComponent";
        _logGroup.componentName = "System";
        _logStream.thingName = system.getValue<Aws::Crt::String>({THING_NAME});
        auto logFilePath = system.getValue<std::string>({"rootpath"})
                           + "/logs/greengrass.log";

        std::string logGroupName =
            "/aws/greengrass/" + _logGroup.componentType + "/" + _logGroup.region + "/" +
            _logGroup.componentName;
        std::string logStreamName = "/" + _logStream.date + "/thing/" +
                                    std::string(_logStream.thingName.c_str(),
                                                _logStream.thingName.size());
        // sign the request

        // set HTTP headers

        // set cURL options

        // perform the request

        // clean up
        curl_easy_cleanup(curl);
    }
}