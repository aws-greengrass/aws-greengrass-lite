#include "iot_broker.hpp"

const auto LOG = ggapi::Logger::of("TES");

ggapi::Struct IotBroker::retrieveToken(ggapi::Task, ggapi::Symbol, ggapi::Struct callData) {
    ggapi::Struct response = ggapi::Struct::create();
    const char *json_string = _savedToken.c_str();
    // TODO: Verify if keys exist before retrieving [Cache]
    auto jsonHandle = ggapi::Buffer::create().put(0, std::string_view{_savedToken}).fromJson();
    auto responseStruct = ggapi::Struct::create();
    auto jsonStruct = ggapi::Struct{jsonHandle};

    if(jsonStruct.hasKey("credentials")) {
        auto innerStruct = jsonStruct.get<ggapi::Struct>("credentials");
        responseStruct.put("AccessKeyId", innerStruct.get<std::string>("accessKeyId"));
        responseStruct.put("SecretAccessKey", innerStruct.get<std::string>("secretAccessKey"));
        responseStruct.put("Token", innerStruct.get<std::string>("sessionToken"));
        responseStruct.put("Expiration", innerStruct.get<std::string>("expiration"));
        // Create json response string
        auto responseBuffer = responseStruct.toJson();
        auto responseVec = responseBuffer.get<std::vector<uint8_t>>(0, responseBuffer.size());
        auto responseJsonAsString = std::string{responseVec.begin(), responseVec.end()};
        response.put("Response", responseJsonAsString);
        return response;
    }

    LOG.atInfo().event("Unable to fetch TES credentials").kv("ERROR", _savedToken).log();
    std::cerr << "[TES] Unable to fetch TES credentials. ERROR: " << _savedToken << std::endl;

    auto responseBuffer = jsonStruct.toJson();
    auto responseVec = responseBuffer.get<std::vector<uint8_t>>(0, responseBuffer.size());
    auto responseJsonAsString = std::string{responseVec.begin(), responseVec.end()};
    response.put("Error", responseJsonAsString);
    return response;
}

bool IotBroker::tesOnStart(ggapi::Struct data) {
    // Read the Device credentials
    auto returnValue = false;
    try {
        auto system = _system.load();
        auto nucleus = _nucleus.load();

        _thingInfo.rootCaPath =
            system.getValue<std::string>({"rootCaPath"}); // OverrideDefaultTrustStore
        _thingInfo.certPath =
            system.getValue<std::string>({"certificateFilePath"}); // InitClientWithMtls
        _thingInfo.keyPath = system.getValue<std::string>({"privateKeyPath"}); // InitClientWithMtls
        _thingInfo.thingName = system.getValue<Aws::Crt::String>({"thingName"}); // Header
        _iotRoleAlias = nucleus.getValue<std::string>({"configuration", "iotRoleAlias"}); // URI

        // TODO: Note, reference of the module name will be done by Nucleus, this is temporary.
        _thingInfo.credEndpoint =
            nucleus.getValue<std::string>({"configuration", "iotCredEndpoint"});

        // TODO:: Validate that these key exist [RoleAlias minimum]

        returnValue = true;
    } catch(const std::exception &e) {
        LOG.atInfo()
            .event("Failed to parse device config for credentials")
            .kv("ERROR", e.what())
            .log();
        std::cerr << "[TES] Error: " << e.what() << std::endl;
    }

    tesRefresh();

    return returnValue;
}

// TODO:: Fix the mutex on TLSConnectionInit failure on refresh
void IotBroker::tesRefresh() {
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

    auto response =
        ggapi::Task::sendToTopic(ggapi::Symbol{"aws.greengrass.fetchTesFromCloud"}, request);

    _savedToken = response.get<std::string>("Response");
}

bool IotBroker::tesOnRun() {
    std::ignore = getScope().subscribeToTopic(
        ggapi::Symbol{"aws.greengrass.requestTES"},
        ggapi::TopicCallback::of(&IotBroker::retrieveToken, this));

    return true;
}
