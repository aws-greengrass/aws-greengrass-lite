#pragma once
#include <filesystem>
#include <forward_list>

#include <aws/crt/io/EventLoopGroup.h>
#include <aws/crt/io/SocketOptions.h>

#include "server_bootstrap.hpp"
#include "server_continuation.hpp"

static void onListenerDestroy(
    aws_event_stream_rpc_server_listener *server, void *user_data) noexcept {
}

class ServerListener {
public:
    using Connection = aws_event_stream_rpc_server_connection;

private:
    std::recursive_mutex stateMutex;
    std::forward_list<Connection *> underlyingConnection;
    Aws::Crt::Allocator *_allocator;
    Aws::Crt::Io::EventLoopGroup _eventLoop{1};
    Aws::Crt::Io::SocketOptions _socketOpts = []() -> auto {
        using namespace Aws::Crt::Io;
        SocketOptions opts{};
        opts.SetSocketDomain(SocketDomain::Local);
        opts.SetSocketType(SocketType::Stream);
        return opts;
    }();
    Aws::Crt::Io::ServerBootstrap _bootstrap{_eventLoop};
    aws_event_stream_rpc_server_listener *listener{};
    static constexpr uint16_t port = 54345;

public:
    ServerListener(const ServerListener &) = delete;
    ServerListener(ServerListener &&) = delete;
    ServerListener &operator=(const ServerListener &) = delete;
    ServerListener &operator=(ServerListener &&) = delete;
    explicit ServerListener(Aws::Crt::Allocator *allocator = Aws::Crt::g_allocator)
        : _allocator(allocator), _eventLoop(1, allocator), _bootstrap(_eventLoop, allocator) {
    }

    ~ServerListener() noexcept {
        Close();
    }

    void Connect(std::string_view socket_path);

    void Disconnect();

    void Close(int shutdownCode = AWS_ERROR_SUCCESS) noexcept;

    int sendConnectionResponse(Connection *conn);

    int sendPingResponse(Connection *conn);

    int sendErrorResponse(
        Connection *conn,
        std::string_view message,
        aws_event_stream_rpc_message_type error_type,
        uint32_t flags);

    static int onNewServerConnection(
        aws_event_stream_rpc_server_connection *connection,
        int error_code,
        aws_event_stream_rpc_connection_options *connection_options,
        void *user_data) noexcept;

    static void onServerConnectionShutdown(
        aws_event_stream_rpc_server_connection *connection,
        int error_code,
        void *user_data) noexcept;

    static void onProtocolMessage(
        aws_event_stream_rpc_server_connection *connection,
        const aws_event_stream_rpc_message_args *message_args,
        void *user_data) noexcept;

    static int onIncomingStream(
        aws_event_stream_rpc_server_connection *connection,
        aws_event_stream_rpc_server_continuation_token *token,
        aws_byte_cursor operation_name,
        aws_event_stream_rpc_server_stream_continuation_options *continuation_options,
        void *user_data) noexcept;
};
