//
// Created by Illa, Saranya on 2/7/24.
#pragma once
#include <iostream>

#include <aws/common/logging.h>
#include <aws/http/private/request_response_impl.h>
#include <aws/http/request_response.h>
#include <aws/http/server.h>
#include <aws/io/channel_bootstrap.h>
#include <aws/io/event_loop.h>
#include <aws/io/logging.h>
#include <aws/io/socket.h>
#include <aws/io/stream.h>
#include <plugin.hpp>

const auto LOG = ggapi::Logger::of("TesHttpServerPlugin");

class TesHttpServer {
private:
    void setup_server();
    void destroy_server();

public:
    TesHttpServer() noexcept = default;
    static TesHttpServer &get() {
        static TesHttpServer instance{};
        return instance;
    }
    void start_server();
    void stop_server();
    ~TesHttpServer() noexcept = default;
};
