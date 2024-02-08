#include "iot_broker.hpp"
#include <rapidjson/document.h>
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

ggapi::Struct IotBroker::retrieveToken(ggapi::Task, ggapi::Symbol, ggapi::Struct callData) {
    ggapi::Struct response = ggapi::Struct::create();
    const char* json_string = _savedToken.c_str();
    // Parse JSON response
    // TODO: Verify if keys exist before retrieving

    rapidjson::Document document;
    document.Parse(json_string);

    rapidjson::Document new_doc;
    new_doc.SetObject();
    auto creds = document["credentials"].GetObject();
    new_doc.AddMember("AccessKeyId", creds["accessKeyId"], new_doc.GetAllocator());
    new_doc.AddMember("SecretAccessKey", creds["secretAccessKey"], new_doc.GetAllocator());
    new_doc.AddMember("Token", creds["sessionToken"], new_doc.GetAllocator());
    new_doc.AddMember("Expiration", creds["expiration"], new_doc.GetAllocator());
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    new_doc.Accept(writer);

    // Send response object
    response.put("Response", buffer.GetString());
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
        ggapi::Task::sendToTopic(ggapi::Symbol{"aws.greengrass.fetch_TES_from_cloud"}, request);

    _savedToken = response.get<std::string>("Response");

    return returnValue;
}

bool IotBroker::tesOnRun(void) {
std::ignore = getScope().subscribeToTopic(
        ggapi::Symbol{"aws.greengrass.requestTES"},
        ggapi::TopicCallback::of(&IotBroker::retrieveToken, this));
    return true;
}
