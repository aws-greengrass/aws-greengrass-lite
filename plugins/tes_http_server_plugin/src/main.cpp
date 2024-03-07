#include "tes_http_server.hpp"
#include <iostream>
#include <plugin.hpp>

class TesHttpServerPlugin : public ggapi::Plugin {
    TesHttpServer _local_server = TesHttpServer::get();

public:
    void beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) override;
    bool onStart(ggapi::Struct data) override;

    bool onRun(ggapi::Struct data) override;
    bool onTerminate(ggapi::Struct data) override;

    static TesHttpServerPlugin &get() {
        static TesHttpServerPlugin instance{};
        return instance;
    }
};

bool TesHttpServerPlugin::onStart(ggapi::Struct data) {
    return true;
}

// TODO: Must verify if TES is running before starting up the HTTP server.
bool TesHttpServerPlugin::onRun(ggapi::Struct data) {
    TesHttpServer::startServer();
    return true;
}

bool TesHttpServerPlugin::onTerminate(ggapi::Struct data) {
    TesHttpServer::stopServer();
    return true;
}

void TesHttpServerPlugin::beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) {
    std::cout << "Running beforeLifecycle of TesHttpServerPlugin" << ggapi::Symbol{phase}.toString()
              << std::endl;
}

extern "C" [[maybe_unused]] ggapiErrorKind greengrass_lifecycle(
    ggapiObjHandle moduleHandle, ggapiSymbol phase, ggapiObjHandle data, bool *pHandled) noexcept {
    return TesHttpServerPlugin::get().lifecycle(moduleHandle, phase, data, pHandled);
}
