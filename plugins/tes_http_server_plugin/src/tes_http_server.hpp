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
    void onInitialize(ggapi::Struct data) override;
    void onStart(ggapi::Struct data) override;
    void onStop(ggapi::Struct data) override;

    static TesHttpServerPlugin &get() {
        static TesHttpServerPlugin instance{};
        return instance;
    }
};
