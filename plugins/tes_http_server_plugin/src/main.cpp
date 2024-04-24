#include "tes_http_server.hpp"
#include <plugin.hpp>

void TesHttpServerPlugin::onInitialize(ggapi::Struct data) {
    std::ignore = util::getDeviceSdkApiHandle(); // Make sure Api initialized
}

// TODO: Must verify if TES is running before starting up the HTTP server.
void TesHttpServerPlugin::onStart(ggapi::Struct data) {
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
}

void TesHttpServerPlugin::onStop(ggapi::Struct data) {
    TesHttpServer::stopServer();
}

extern "C" [[maybe_unused]] ggapiErrorKind greengrass_lifecycle(
    ggapiObjHandle moduleHandle, ggapiSymbol phase, ggapiObjHandle data) noexcept {
    return TesHttpServerPlugin::get().lifecycle(moduleHandle, phase, data);
}
