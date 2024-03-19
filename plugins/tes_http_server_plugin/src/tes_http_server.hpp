#pragma once
#include <iostream>
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
