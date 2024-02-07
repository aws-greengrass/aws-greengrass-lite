#include "iot_broker.hpp"

ggapi::Struct IotBroker::retriveToken(ggapi::Task, ggapi::Symbol, ggapi::Struct callData) {
    ggapi::Struct response = ggapi::Struct::create();
    response.put("Response", _savedToken.c_str());
    return response;
}

bool IotBroker::tesOnStart(ggapi::Struct data) {
    // Read the Device credintails
    // Make a http Requst to
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
        std::cerr << "[TES] Error: " << e.what() << std::endl;
    }

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
        ggapi::Task::sendToTopic(ggapi::Symbol{"aws.grengrass.fetch_TES_from_cloud"}, request);

    _savedToken = response.get<std::string>("Response");

    return returnValue;
}

bool IotBroker::tesOnRun(void) {
    std::ignore = getScope().subscribeToTopic(
        ggapi::Symbol{"aws.grengrass.requestTES"},
        ggapi::TopicCallback::of(&IotBroker::retriveToken, this));
    return true;
}
