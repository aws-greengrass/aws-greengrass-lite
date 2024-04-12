#pragma once
#include <plugin.hpp>
#include <shared_device_sdk.hpp>

class TesHttpServer {
public:
    static TesHttpServer &get() {
        static TesHttpServer instance{};
        return instance;
    }
    static void startServer();
    static void stopServer();
};

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

inline bool TesHttpServerPlugin::onInitialize(ggapi::Struct data) {
    std::ignore = util::getDeviceSdkApiHandle(); // Make sure Api initialized
    return true;
}

// TODO: Must verify if TES is running before starting up the HTTP server.
inline bool TesHttpServerPlugin::onStart(ggapi::Struct data) {
    TesHttpServer::startServer();
    return true;
}

inline bool TesHttpServerPlugin::onStop(ggapi::Struct data) {
    TesHttpServer::stopServer();
    return true;
}

inline bool TesHttpServerPlugin::onError_stop(ggapi::Struct data) {
    return true;
}