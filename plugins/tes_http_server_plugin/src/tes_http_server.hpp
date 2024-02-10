#pragma once
#include <iostream>

#include <aws/common/logging.h>
#include <aws/http/connection.h>
#include <aws/http/private/request_response_impl.h>
#include <aws/http/request_response.h>
#include <aws/http/server.h>
#include <aws/http/status_code.h>
#include <aws/io/channel_bootstrap.h>
#include <aws/io/event_loop.h>
#include <aws/io/logging.h>
#include <aws/io/socket.h>
#include <aws/io/stream.h>
#include <plugin.hpp>

class TesHttpServer {
public:
    static TesHttpServer &get() {
        static TesHttpServer instance{};
        return instance;
    }
    static void startServer();
    static void stopServer();
};
