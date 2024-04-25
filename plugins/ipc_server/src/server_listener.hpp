#pragma once
#include "message.hpp"
#include "server_bootstrap.hpp"
#include <api_standard_errors.hpp>
#include <auto_release.hpp>
#include <cpp_api.hpp>
#include <filesystem>
#include <forward_list>
#include <map>
#include <shared_device_sdk.hpp>

namespace ipc_server {

    using AwsListenerResource = util::AutoReleasePtr<aws_event_stream_rpc_server_listener>;
    using AwsConnection = util::AutoReleasePtr<aws_event_stream_rpc_server_connection>;

    class ServerConnection;

    /**
     * This class manages listening on a single IPC socket. As connections come in, it is
     * responsible for creating and delegating to ServerConnection for each connection.
     */
    class ServerListener : public util::RefObject<ServerListener> {

        // Handle is set once by setHandleRef, and is intentionally opaque
        void *_handle{nullptr};

        ggapi::ModuleScope _module;
        mutable std::shared_mutex _stateMutex;
        std::map<void *, std::shared_ptr<ServerConnection>> _connections;
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
        AwsListenerResource _listener;
        std::atomic<bool> _closing{false};

    public:
        ServerListener(const ServerListener &) = delete;
        ServerListener(ServerListener &&) = delete;
        ServerListener &operator=(const ServerListener &) = delete;
        ServerListener &operator=(ServerListener &&) = delete;
        explicit ServerListener(
            ggapi::ModuleScope module, Aws::Crt::Allocator *allocator = Aws::Crt::g_allocator)
            : _module(std::move(module)), _allocator(allocator), _eventLoop(1, allocator),
              _bootstrap(_eventLoop, allocator) {

            _listener.setRelease(aws_event_stream_rpc_server_listener_release);
        }

        ~ServerListener() noexcept {
            close();
        }

        /**
         * Expected to be called immediately after construction
         */
        void setHandleRef(void *handle) {
            _handle = handle;
        }

        void connect(const std::string &socket_path);

        void close() noexcept;

        [[nodiscard]] ggapi::ModuleScope module() const {
            return _module;
        }

        static int onNewServerConnection(
            aws_event_stream_rpc_server_connection *awsConnection,
            int error_code,
            aws_event_stream_rpc_connection_options *connection_options,
            void *user_data) noexcept;

        int onNewServerConnectionImpl(
            aws_event_stream_rpc_server_connection *awsConnection,
            int error_code,
            aws_event_stream_rpc_connection_options *connection_options) noexcept;

        static void onServerConnectionShutdown(
            aws_event_stream_rpc_server_connection *awsConnection,
            int error_code,
            void *user_data) noexcept;

        void onServerConnectionShutdownImpl(
            aws_event_stream_rpc_server_connection *awsConnection, int error_code) noexcept;

        void removeConnection(aws_event_stream_rpc_server_connection *awsConnection) noexcept;

        static void onListenerDestroy(
            aws_event_stream_rpc_server_listener *server, void *user_data) noexcept;
    };

} // namespace ipc_server
