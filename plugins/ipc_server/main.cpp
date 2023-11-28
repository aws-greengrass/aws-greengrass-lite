#include <aws/crt/Allocator.h>
#include <aws/crt/Api.h>
#include <aws/crt/io/EventLoopGroup.h>
#include <aws/crt/io/SocketOptions.h>
#include <aws/event-stream/event_stream_rpc_server.h>
#include <aws/io/channel_bootstrap.h>
#include <iostream>
#include <plugin.hpp>
#include <stdexcept>
#include <system_error>
#include <tuple>

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
        opts.SetSocketDomain(SocketDomain::IPv4);
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
    std::ignore = connection_options;
    std::ignore = user_data;

    return AWS_OP_SUCCESS;
}

static void s_fixture_on_server_connection_shutdown(
    struct aws_event_stream_rpc_server_connection *connection, int error_code, void *user_data) {
    std::ignore = connection;
    std::ignore = error_code;
    std::ignore = user_data;
}

static void s_on_listener_destroy(
    struct aws_event_stream_rpc_server_listener *server, void *user_data) {
}

bool IpcServer::onStart(ggapi::Struct data) {

    aws_event_stream_rpc_server_listener_options listenerOptions = {
        .host_name = "127.0.0.1",
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

    std::cerr << "Wow 🙀" << std::endl;

    return false;
}
