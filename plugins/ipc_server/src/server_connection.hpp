#pragma once
#include "message.hpp"
#include <api_standard_errors.hpp>
#include <auto_release.hpp>
#include <cpp_api.hpp>
#include <filesystem>
#include <forward_list>
#include <shared_device_sdk.hpp>

namespace ipc_server {
    class ServerListener;
    class ConnectionStream;

    using AwsConnection = util::AutoReleasePtr<aws_event_stream_rpc_server_connection>;

    /**
     * This class manages a single IPC connection. Typically there is a single connection per
     * process, but not required or enforced. There are multiple incoming connections per
     * ServerListener, and multiple ConnectionStream's per connection. As requests come in, it is
     * responsible for creating a continuation stream that takes over until the request has been
     * completed.
     */
    class ServerConnection : public util::RefObject<ServerConnection> {

        // Handle is set once by setHandleRef, and is intentionally opaque
        void *_handle{nullptr};

        std::weak_ptr<ServerListener> _listener;
        ggapi::ModuleScope _module;
        mutable std::shared_mutex _stateMutex;
        AwsConnection _connection;
        std::map<void *, std::weak_ptr<ConnectionStream>> _streams;
        std::string _connectedServiceName;
        // std::atomic_flag would fit in C++20, but not C++17
        std::atomic<bool> _authenticated{false};

    private:
        std::string getServiceNameFromToken(const std::string &token);

    public:
        ServerConnection(const ServerConnection &) = delete;
        ServerConnection(ServerConnection &&) = delete;
        ServerConnection &operator=(const ServerConnection &) = delete;
        ServerConnection &operator=(ServerConnection &&) = delete;
        explicit ServerConnection(
            const std::shared_ptr<ServerListener> &listener,
            ggapi::ModuleScope module,
            AwsConnection connection)
            : _listener(listener), _module(std::move(module)), _connection(std::move(connection)) {
        }

        ~ServerConnection() noexcept {
            close();
        }

        [[nodiscard]] uintptr_t id() const noexcept {
            // This ID is intended to allow correlation with AWS logs
            // NOLINTNEXTLINE(*-reinterpret-cast)
            return reinterpret_cast<uintptr_t>(_connection.get());
        }

        void initOptions(aws_event_stream_rpc_connection_options &options) noexcept;

        /**
         * Make a correctly ref-counted copy of the connection.
         * @return ref-counted copy
         */
        AwsConnection connection() const noexcept;

        /**
         * Expected to be called immediately after construction
         */
        void setHandleRef(void *handle) {
            _handle = handle;
        }

        std::string getConnectedServiceName();

        void close() noexcept;
        void onShutdown(int error_code) noexcept;

        void onConnect(const Message &message);

        void onPing(const Message &message);

        void onPingResponse(const Message &message) const;

        ggapi::Future sendProtocolMessage(const Message &message) const;

        static void onCompleteSend(int error_code, void *user_data) noexcept;

        [[nodiscard]] ggapi::ModuleScope module() const {
            return _module;
        }

        static void onProtocolMessage(
            aws_event_stream_rpc_server_connection *connection,
            const aws_event_stream_rpc_message_args *message_args,
            void *user_data) noexcept;

        void onProtocolMessageImpl(
            aws_event_stream_rpc_server_connection *connection,
            const aws_event_stream_rpc_message_args *message_args) noexcept;

        static int onIncomingStream(
            aws_event_stream_rpc_server_connection *connection,
            aws_event_stream_rpc_server_continuation_token *token,
            aws_byte_cursor operation_name,
            aws_event_stream_rpc_server_stream_continuation_options *continuation_options,
            void *user_data) noexcept;

        [[nodiscard]] int onIncomingStreamImpl(
            aws_event_stream_rpc_server_connection *connection,
            aws_event_stream_rpc_server_continuation_token *token,
            aws_byte_cursor operation_name,
            aws_event_stream_rpc_server_stream_continuation_options *continuation_options) noexcept;
    };

} // namespace ipc_server
