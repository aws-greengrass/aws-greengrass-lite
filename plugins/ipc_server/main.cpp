#include "util.hpp"
#include <aws/crt/Allocator.h>
#include <aws/crt/Api.h>
#include <aws/crt/io/EventLoopGroup.h>
#include <aws/crt/io/SocketOptions.h>
#include <aws/event-stream/event_stream_rpc_server.h>
#include <aws/io/channel_bootstrap.h>
#include <cstdio>
#include <filesystem>
#include <future>
#include <iostream>
#include <ostream>
#include <plugin.hpp>
#include <stdexcept>
#include <system_error>
#include <tuple>

static void s_fixture_on_protocol_message(
    struct aws_event_stream_rpc_server_connection *connection,
    const struct aws_event_stream_rpc_message_args *message_args,
    void *user_data) {
    std::cerr << "😹 called s_fixture_on_protocol_message" << std::endl;
    for(auto item : util::Span{message_args->headers, message_args->headers_count}) {
    }
}
static int on_incoming_stream(
    struct aws_event_stream_rpc_server_connection *connection,
    struct aws_event_stream_rpc_server_continuation_token *token,
    struct aws_byte_cursor operation_name,
    struct aws_event_stream_rpc_server_stream_continuation_options *continuation_options,
    void *user_data) {
    std::cerr << "😹 called on_server_incoming_stream" << std::endl;

    return AWS_OP_SUCCESS;
}

class IpcServer : public ggapi::Plugin {

public:
    bool onBootstrap(ggapi::Struct data) override;
    bool onBind(ggapi::Struct data) override;
    bool onStart(ggapi::Struct data) override;
    bool onTerminate(ggapi::Struct data) override;
    void beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) override;

    static IpcServer &get() {
        static IpcServer instance{};
        return instance;
    }

private:
    Aws::Crt::Io::EventLoopGroup eventLoop{1};
    Aws::Crt::Allocator *alloc = Aws::Crt::DefaultAllocatorImplementation();
    Aws::Crt::Io::SocketOptions socketOpts = []() -> auto {
        using namespace Aws::Crt::Io;
        SocketOptions opts{};
        opts.SetSocketDomain(SocketDomain::Local);
        opts.SetSocketType(SocketType::Stream);
        return opts;
    }();
    static constexpr uint16_t port = 54345;
    aws_server_bootstrap *bootstrap =
        aws_server_bootstrap_new(alloc, eventLoop.GetUnderlyingHandle());
};

// Initializes global CRT API
// TODO: What happens when multiple plugins use the CRT?
static const Aws::Crt::ApiHandle apiHandle{};

extern "C" [[maybe_unused]] bool greengrass_lifecycle(
    uint32_t moduleHandle, uint32_t phase, uint32_t dataHandle) noexcept {
    return IpcServer::get().lifecycle(moduleHandle, phase, dataHandle);
}

void IpcServer::beforeLifecycle(ggapi::Symbol phase, ggapi::Struct data) {
    std::cerr << "[mqtt-plugin] Running lifecycle phase " << phase.toString() << std::endl;
}

bool IpcServer::onBootstrap(ggapi::Struct structData) {
    structData.put("name", "aws.greengrass.ipc_server");
    return true;
}

bool IpcServer::onTerminate(ggapi::Struct structData) {
    return true;
}

bool IpcServer::onBind(ggapi::Struct data) {
    return true;
}

static int s_fixture_on_new_server_connection(
    struct aws_event_stream_rpc_server_connection *connection,
    int error_code,
    struct aws_event_stream_rpc_connection_options *connection_options,
    void *user_data) {
    std::ignore = connection;
    std::ignore = error_code;
    std::ignore = user_data;

    *connection_options = {
        .on_incoming_stream = on_incoming_stream,
        .on_connection_protocol_message = s_fixture_on_protocol_message,
        .user_data = nullptr,
    };

    std::cerr << "😾 s_on_new_server_connection called " << error_code << std::endl;
    return AWS_OP_SUCCESS;
}

static void s_fixture_on_server_connection_shutdown(
    struct aws_event_stream_rpc_server_connection *connection, int error_code, void *user_data) {
    std::ignore = connection;
    std::ignore = error_code;
    std::ignore = user_data;
    std::cerr << "😾 s_on_server_connection_shutdown called: " << error_code << std::endl;
}

static void s_on_listener_destroy(
    struct aws_event_stream_rpc_server_listener *server, void *user_data) {
    std::cerr << "😾 s_on_listener_destroy called." << std::endl;
}

bool IpcServer::onStart(ggapi::Struct data) {
    std::cerr << "onStart started 😹" << std::endl;
    static constexpr std::string_view socket_path = "/tmp/gglite-ipc.socket";

    if(std::filesystem::exists(socket_path)) {
        std::filesystem::remove(socket_path);
    }

    aws_event_stream_rpc_server_listener_options listenerOptions = {
        .host_name = socket_path.data(),
        .port = port,
        .socket_options = &socketOpts.GetImpl(),
        .bootstrap = bootstrap,
        .on_new_connection = s_fixture_on_new_server_connection,
        .on_connection_shutdown = s_fixture_on_server_connection_shutdown,
        .on_destroy_callback = s_on_listener_destroy,
        .user_data = nullptr,
    };

    auto *listener = aws_event_stream_rpc_server_new_listener(alloc, &listenerOptions);

    if(!listener) {
        int error_code = aws_last_error();
        throw std::runtime_error("Failed to create RPC server 😿 " + std::to_string(error_code));
    }

    aws_event_stream_rpc_server_continuation_is_closed;

    std::cerr << "Wow 🙀" << std::endl;

    return false;
}
