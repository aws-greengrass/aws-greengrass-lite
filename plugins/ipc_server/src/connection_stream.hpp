#pragma once
#include "ipc_server.hpp"
#include "message.hpp"
#include "server_connection.hpp"
#include <api_standard_errors.hpp>
#include <auto_release.hpp>
#include <plugin.hpp>
#include <shared_device_sdk.hpp>

namespace ipc_server {
    class ServerListener;
    class ServerConnection;

    using AwsConnection = util::AutoReleasePtr<aws_event_stream_rpc_server_connection>;
    using AwsToken = util::AutoReleasePtr<aws_event_stream_rpc_server_continuation_token>;

    /**
     * A stream is a sequence of messages that implements a single IPC request, initial response,
     * and streamed response. The token individually identifies a single stream, that is, request.
     * All streams (requests) are associated with a single connection (many to one).
     */
    class ConnectionStream : public util::RefObject<ConnectionStream> {

    public:
        enum class State { Begin, Command, Responding, Terminate };

    private:
        // Handle is set once by setHandleRef, and is intentionally opaque
        void *_handle{nullptr};

        std::weak_ptr<ServerConnection> _connection;
        ggapi::ModuleScope _module;
        mutable std::shared_mutex _stateMutex;
        AwsToken _token;
        const std::string _operation;
        ggapi::Channel _channel{};
        std::atomic<State> _state{State::Begin};

    public:
        ConnectionStream(const ConnectionStream &) = delete;
        ConnectionStream(ConnectionStream &&) = delete;
        ConnectionStream &operator=(const ConnectionStream &) = delete;
        ConnectionStream &operator=(ConnectionStream &&) = delete;
        explicit ConnectionStream(
            const std::shared_ptr<ServerConnection> &connection,
            ggapi::ModuleScope module,
            AwsToken token,
            std::string operation)
            : _connection(connection), _module{std::move(module)}, _token{std::move(token)},
              _operation{std::move(operation)} {
        }

        ~ConnectionStream() noexcept {
            if(_channel) {
                try {
                    _channel.close();
                } catch(...) {
                    // Best effort
                }
            }
        }

        [[nodiscard]] uintptr_t tokenId() const noexcept {
            // This ID is intended to allow correlation with AWS logs
            // NOLINTNEXTLINE(*-reinterpret-cast)
            return reinterpret_cast<uintptr_t>(_token.get());
        }

        [[nodiscard]] uintptr_t connectionId() const noexcept;

        void initOptions(aws_event_stream_rpc_server_stream_continuation_options &options) noexcept;

        std::shared_ptr<ServerConnection> connection() const noexcept;

        /**
         * Safe (ref-counted) copy of token
         */
        AwsToken token() const noexcept;

        /**
         * Expected to be called (almost) immediately after construction
         */
        void setHandleRef(void *handle) {
            _handle = handle;
        }

        [[nodiscard]] std::string operation() const {
            return _operation;
        }

        [[nodiscard]] std::string lpcTopic() const {
            return "IPC::" + _operation;
        }

        [[nodiscard]] std::string ipcServiceModel() const {
            return _operation + "Response";
        }

        [[nodiscard]] ggapi::ModuleScope module() const {
            return _module;
        }

        /**
         * Callback after stream has been configured.
         */
        void onAccept();

        /**
         * Performs first level validation of operation prior to trying to
         * dispatch.
         */
        void verifyOperation() const;

        /**
         * Attempts to dispatch operation.
         */
        void dispatch(const Message &message);
        void dispatchAsync(
            const std::shared_ptr<ConnectionStream> &, const Message &message) noexcept;

        void firstResponseAsync(
            const std::shared_ptr<ConnectionStream> &, const ggapi::Future &future) noexcept;
        void firstResponse(const ggapi::Future &future);

        void connectChannel(ggapi::Channel channel);

        static void onChannelCallback(
            const std::weak_ptr<ConnectionStream> &weakSelf, const ggapi::ObjHandle &response);
        static void onChannelCallbackDeferred(
            const std::weak_ptr<ConnectionStream> &weakSelf, const ggapi::Future &future) noexcept;

        void onReceiveResponse(const ggapi::Struct &response);
        ggapi::Future sendMessage(const Message &message);
        void sendErrorMessage(const ggapi::GgApiError &error) noexcept;
        static void onCompleteSend(int error_code, void *user_data);

        static void onContinuation(
            aws_event_stream_rpc_server_continuation_token *token,
            const aws_event_stream_rpc_message_args *message_args,
            void *user_data) noexcept;

        void onContinuationImpl(
            aws_event_stream_rpc_server_continuation_token *token,
            const aws_event_stream_rpc_message_args *message_args);

        static void onContinuationClose(
            aws_event_stream_rpc_server_continuation_token *token, void *user_data) noexcept;

        void onContinuationCloseImpl(
            aws_event_stream_rpc_server_continuation_token *token) noexcept;
    };

} // namespace ipc_server
