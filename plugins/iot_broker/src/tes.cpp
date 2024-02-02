#include "iot_broker.hpp"

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
        _iotroleAlias = nucleus.getValue<std::string>({"configuration", "iotRoleAlias"}); // URI

        // TODO: Note, reference of the module name will be done by Nucleus, this is temporary.
        _thingInfo.credEndpoint =
            nucleus.getValue<std::string>({"configuration", "iotCredEndpoint"});

        // TODO:: Validate that these key exist [RoleAlias minimum]

        returnValue = true;
    } catch(const std::exception &e) {
        std::cerr << "[TES] Error: " << e.what() << std::endl;
    }

    // TODO: Pass all the above Values to the LPC on the topic .TesFetchToken
    // The Response will be a json with token
    // Sample SingleLine JSON:
    // {    "credentials":{"accessKeyId":"TestKeyId","secretAccessKey":"TestSecrectKey",
    //      "sessionToken":"SampleTokenIQoJb3JpZ2luX2VjEIf//////////wEaCXVzLXdlc3QtMiJHMEUCIAGBvpv5bia9gcPfEGpo8BP/msQvGS8OoOEnZbh8Vop+AiEA1vSCuwi
    //                      AwxvRht3IYFQ7PS9y1u3EWJ4Yl1nuGkmroloqvgMIUBABGgw3NTQyODE5MTU0NzEiDBfDzPksj2L7U0g99CqbA0mymgMKcXmZrh7f1ENrZF
    //                      jqWAX5matoq7vmvtNwezT4V6IAFjSPzhS0d1wUFeQeWDnIk97kq8CWhSKbxjQxHZwGb+PjhNi28RtHP0COyfuxpHj7JbmpkrkzgZRz8LaLz
    //                      IFNzvXc5/cEdeWiBWstqcFZbj9xFEEm6xyVGWjLVxlViiKG6c10p0qZ5UCZ/50afBUYlhyhmVvtTXE1SVA9lUzRyzIcw2ztVsQ==",
    //      "expiration":"2024-02-01T23:46:23Z"}}

    return returnValue;
}
