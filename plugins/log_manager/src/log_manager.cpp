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
    readDeviceInfo();
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

void LogManager::readDeviceInfo() {
    std::shared_lock guard{_mutex};
    try {
        auto system = _system;
        auto nucleus = _nucleus;

        _thingInfo.rootCaPath = system.getValue<std::string>({ROOT_CA_PATH});
        _thingInfo.certPath = system.getValue<std::string>({CERT_PATH});
        _thingInfo.keyPath = system.getValue<std::string>({PRIVATE_KEY_PATH});
        _thingInfo.thingName = system.getValue<Aws::Crt::String>({THING_NAME});
        _iotRoleAlias = nucleus.getValue<std::string>({CONFIGURATION_KEY, IOT_ROLE_ALIAS});

        // TODO: Note, reference of the module name will be done by Nucleus, this is temporary.
        _thingInfo.credEndpoint =
            nucleus.getValue<std::string>({CONFIGURATION_KEY, IOT_CRED_ENDPOINT});
    } catch(const std::exception &e) {
        LOG.atInfo()
            .event("Failed to parse device config for credentials")
            .kv("ERROR", e.what())
            .log();
        std::cerr << "[TES] Error: " << e.what() << std::endl;
    }
    guard.unlock();
}

void LogManager::retrieveCredentialsFromTES() {
    auto request{ggapi::Struct::create()};
    std::stringstream ss;
    ss << "https://" << _thingInfo.credEndpoint << "/role-aliases/" << _iotRoleAlias
       << "/credentials";

    request.put("uri", ss.str());
    request.put("thingName", _thingInfo.thingName.c_str());
    request.put("certPath", _thingInfo.certPath.c_str());
    size_t found = _thingInfo.rootCaPath.find_last_of("/");
    std::string caDirPath = _thingInfo.rootCaPath.substr(0, found);
    request.put("caPath", caDirPath.c_str());
    request.put("caFile", _thingInfo.rootCaPath.c_str());
    request.put("pkeyPath", _thingInfo.keyPath.c_str());

    auto future = ggapi::Subscription::callTopicFirst(
        ggapi::Symbol{"aws.greengrass.fetchTesFromCloud"}, request);
    // TODO: Handle case when resultFuture is empty (no handlers)
    auto response = ggapi::Struct(future.waitAndGetValue());

    std::unique_lock guard{_mutex};
    _savedToken = response.get<std::string>("Response");
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
        auto logFilePath = system.getValue<std::string>({"rootpath"})
                           + "/logs/greengrass.log";

        std::string logGroupName =
            "/aws/greengrass/" + _logGroup.componentType + "/" + _logGroup.region + "/" +
            _logGroup.componentName;
        std::string logStreamName = "/" + _logStream.date + "/thing/" +
                                    std::string(_thingInfo.thingName.c_str(),
                                                _thingInfo.thingName.size());
        // sign the request

        // set HTTP headers

        // set cURL options

        // perform the request

        // clean up
        curl_easy_cleanup(curl);
    }
}