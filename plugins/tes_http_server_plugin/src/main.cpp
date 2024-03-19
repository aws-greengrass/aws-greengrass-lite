#include "tes_http_server.hpp"
#include <plugin.hpp>

class TesHttpServerPlugin : public ggapi::Plugin {
    TesHttpServer _local_server = TesHttpServer::get();

public:
    bool onInitialize(ggapi::Struct data) override;
    bool onStart(ggapi::Struct data) override;
    bool onStop(ggapi::Struct data) override;
    bool onError_stop(ggapi::Struct data) override;

    static TesHttpServerPlugin &get() {
        static TesHttpServerPlugin instance{};
        return instance;
    }
};

bool TesHttpServerPlugin::onInitialize(ggapi::Struct data) {
    std::ignore = util::getDeviceSdkApiHandle(); // Make sure Api initialized
    return true;
}

// TODO: Must verify if TES is running before starting up the HTTP server.
bool TesHttpServerPlugin::onStart(ggapi::Struct data) {
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
    TesHttpServer::startServer();
    return true;
}

bool TesHttpServerPlugin::onStop(ggapi::Struct data) {
    TesHttpServer::stopServer();
    return true;
}

bool TesHttpServerPlugin::onError_stop(ggapi::Struct data) {
    return true;
}

extern "C" [[maybe_unused]] ggapiErrorKind greengrass_lifecycle(
    ggapiObjHandle moduleHandle, ggapiSymbol phase, ggapiObjHandle data, bool *pHandled) noexcept {
    return TesHttpServerPlugin::get().lifecycle(moduleHandle, phase, data, pHandled);
}
