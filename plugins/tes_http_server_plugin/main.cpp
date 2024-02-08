#include <iostream>
#include <stdio.h>

#include "src/tes_http_server.hpp"
#include <aws/crt/Api.h>
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

// Uncomment this to enable SDK Logging
// static Aws::Crt::ApiHandle apiHandle{};

// TODO: Must verify if TES is running before starting up the HTTP server.
bool TesHttpServerPlugin::onRun(ggapi::Struct data) {
    // Uncomment this to enable SDK Logging

    /* static std::once_flag loggingInitialized;
      try {
         std::call_once(loggingInitialized, []() {
              apiHandle.InitializeLogging(Aws::Crt::LogLevel::Debug, stderr);
          });
      } catch(const std::exception &e) {
          std::cerr << "[he-plugin] probably did not initialize the logging: " << e.what()
                    << std::endl;
    */
    _local_server.start_server();
    return true;
}

bool TesHttpServerPlugin::onTerminate(ggapi::Struct data) {
    _local_server.stop_server();
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
