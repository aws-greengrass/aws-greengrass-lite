#include "server_connection.hpp"
#include "bound_promise.hpp"
#include "connection_stream.hpp"
#include "ipc_server.hpp"
#include "server_listener.hpp"
#include <string_util.hpp>
#include <temp_module.hpp>

namespace ipc_server {

    static const auto LOG = // NOLINT(cert-err58-cpp)
        ggapi::Logger::of("com.aws.greengrass.ipc_server.connection");

    AwsConnection ServerConnection::connection() const noexcept {
        std::shared_lock guard{_stateMutex};
        auto awsConnection = _connection.get();
        aws_event_stream_rpc_server_connection_acquire(awsConnection);
        return {aws_event_stream_rpc_server_connection_release, awsConnection};
    }

    void ServerConnection::initOptions(aws_event_stream_rpc_connection_options &options) noexcept {
        options = {};
        options.on_incoming_stream = onIncomingStream;
        options.on_connection_protocol_message = onProtocolMessage;
        options.user_data = _handle;
    }

    void ServerConnection::close() noexcept {
        aws_event_stream_rpc_server_connection *awsConnection;
        std::shared_ptr<ServerListener> listener;
        {
            std::unique_lock guard{_stateMutex};
            awsConnection = _connection.get();
            listener = _listener.lock();
        }
        if(!awsConnection) {
            return;
        }
        LOG.atInfo("close").kv("id", id()).log("Closing connection/channel");

        // TODO: is this needed?
        if(listener) {
            // remove connection, this will likely drop an onDisconnect message
            // listener->removeConnection(awsConnection);
        }

        aws_event_stream_rpc_server_connection_close(awsConnection, AWS_IO_SOCKET_CLOSED);

        //_connection.release();
    }

    void ServerConnection::onShutdown(int error_code) noexcept {
        if(error_code == AWS_IO_SOCKET_CLOSED) {
            LOG.atDebug("shutdown").kv("id", id()).log("[IPC] connection closed");
        } else {
            util::AwsSdkError err(error_code, "[IPC] connection closed with error");
            LOG.atError("shutdown")
                .kv("id", id())
                .cause(err)
                .log("[IPC] connection closed with error");
        }
        _connection.release();
    }

    void ServerConnection::onProtocolMessage(
        aws_event_stream_rpc_server_connection *connection,
        const aws_event_stream_rpc_message_args *message_args,
        void *user_data) noexcept {

        try {
            IpcServer::connections().invoke(
                user_data, &ServerConnection::onProtocolMessageImpl, connection, message_args);
        } catch(...) {
            IpcServer::logFatal(
                std::current_exception(), "Error trying to dispatch protocol message");
        }
    }

    void ServerConnection::onProtocolMessageImpl(
        aws_event_stream_rpc_server_connection *,
        const aws_event_stream_rpc_message_args *message_args) noexcept {

        util::TempModule tempModule{module()};

        try {
            try {
                auto message = Message::parse(*message_args);

                LOG.atTrace("protocol-message")
                    .kv("id", id())
                    .logStream([&](std::ostream &str) -> void {
                        str << "Received protocol message: " << message;
                    });

                switch(message.getType()) {
                    case AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_CONNECT:
                        onConnect(message);
                        return;
                    case AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_PING:
                        onPing(message);
                        return;
                    case AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_PING_RESPONSE:
                        onPingResponse(message);
                        break;
                    default:
                        LOG.atError("protocolMessage").logStream([&](auto &str) {
                            str << "Unhandled message type " << message.getType() << '\n';
                        });
                        auto msg = Message::ofError(
                            "Unrecognized Message type: value: "
                            + std::to_string(
                                static_cast<
                                    std::underlying_type_t<aws_event_stream_rpc_message_type>>(
                                    message_args->message_type))
                            + " is not recognized as a valid request path.");
                        msg.setTerminateStream();
                        sendProtocolMessage(msg);
                }
            } catch(...) {
                throw ggapi::GgApiError::of(std::current_exception());
            }
        } catch(ggapi::GgApiError &err) {
            LOG.atError("protocolMessage").cause(err).log("Error processing protocol message");
            try {
                // Translate error
                Message resp;
                if(message_args->message_type == AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_CONNECT) {
                    // Special case, an exception becomes a connect-ack failure
                    LOG.atError("connectFailed").cause(err).log("Replying as connect failure");
                    resp.setType(AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_CONNECT_ACK);
                    resp.setConnectionAccepted(false);
                } else {
                    // All other cases, reply as an error
                    resp = Message::ofError(err);
                }
                // better would be resp.setTerminateStream() but there is no stream to close
                auto future = sendProtocolMessage(resp);
                future.whenValid([this](const ggapi::Future &) { close(); });
            } catch(...) {
                LOG.atError("sendErrorFailed").log("Cannot reply with error - just closing");
                close();
            }
        }
    }

    ggapi::Future ServerConnection::sendProtocolMessage(const Message &message) const {
        Message copy(message);
        auto &formatted = copy.prepare();
        AwsConnection awsConnection = connection();
        std::shared_ptr<BoundPromise> bound;
        auto handle = IpcServer::beginPromise(module(), bound);
        int code = aws_event_stream_rpc_server_connection_send_protocol_message(
            awsConnection.get(), &formatted, ServerConnection::onCompleteSend, handle);
        ggapi::Future f;
        if(code) {
            f = IpcServer::failPromise(
                handle, util::AwsSdkError(code, "Protocol send failed (initial)"));
        } else {
            f = bound->promise.toFuture();
        }
        f.whenValid([](const ggapi::Future &future) {
            try {
                std::ignore = future.getValue();
                // LOG.atTrace().log("Sent");
            } catch(ggapi::GgApiError &err) {
                LOG.atWarn("protocolSendError").cause(err).log(err.what());
            }
        });
        return f;
    }

    /**
     * C style callback when send completed - completes associated promise
     *
     * @param error_code 0 on success, else error
     * @param user_data Data passed in during send
     */
    void ServerConnection::onCompleteSend(int error_code, void *user_data) noexcept {
        if(error_code) {
            IpcServer::failPromise(
                user_data, util::AwsSdkError(error_code, "Protocol send failed (async)"));
        } else {
            IpcServer::completePromise(user_data, {}); // No useful data to convey
        }
    }

    int ServerConnection::onIncomingStream(
        aws_event_stream_rpc_server_connection *connection,
        aws_event_stream_rpc_server_continuation_token *token,
        aws_byte_cursor operation_name,
        aws_event_stream_rpc_server_stream_continuation_options *continuation_options,
        void *user_data) noexcept {

        try {
            return IpcServer::connections().invoke(
                user_data,
                &ServerConnection::onIncomingStreamImpl,
                connection,
                token,
                operation_name,
                continuation_options);
        } catch(...) {
            IpcServer::logFatal(
                std::current_exception(), "Error trying to dispatch incoming stream");
            return AWS_OP_ERR;
        }
    }

    int ServerConnection::onIncomingStreamImpl(
        aws_event_stream_rpc_server_connection *,
        aws_event_stream_rpc_server_continuation_token *token,
        aws_byte_cursor operation_name,
        aws_event_stream_rpc_server_stream_continuation_options *continuation_options) noexcept {

        util::TempModule tempModule{module()};

        if(!_authenticated.load()) {
            // This should be impossible to hit and indicates a DeviceSDK error
            LOG.atError().log("Unexpected state - onConnect expected");
            return AWS_OP_ERR;
        }

        // Note, other validation is deferred until first message on the stream, as the
        // stream has to be fully established to get a meaningful error

        try {
            try {
                aws_event_stream_rpc_server_continuation_acquire(token);
                AwsToken refToken(aws_event_stream_rpc_server_continuation_release, token);

                auto operationNameAsSV{Aws::Crt::ByteCursorToStringView(operation_name)};
                std::string operationName{operationNameAsSV.data(), operationNameAsSV.size()};

                auto managed = std::make_shared<ConnectionStream>(
                    baseRef(), module(), std::move(refToken), std::move(operationName));
                managed->setHandleRef(IpcServer::streams().addAsPtr(managed));
                managed->initOptions(*continuation_options);

                {
                    std::unique_lock guard{_stateMutex};
                    _streams.emplace(token, managed);
                }

                managed->onAccept();

                return AWS_OP_SUCCESS;
            } catch(...) {
                throw ggapi::GgApiError::of(std::current_exception());
            }
        } catch(ggapi::GgApiError &err) {
            LOG.atError("incomingStreamError")
                .cause(err)
                .log("Exception while establishing stream");
            return AWS_OP_ERR;
        }
    }

    /**
     * Client/Server handshake.
     * @param message
     */
    void ServerConnection::onConnect(const Message &message) {
        // Note that the RPC C-library is responsible for protecting that there is exactly
        // one authentication connect message - however defense in depth requires additional
        // checks at this layer too.

        if(_authenticated.load()) {
            throw ggapi::GgApiError("Already authenticated");
        }

        // TODO: Validate version header
        // TODO: Validate Message

        auto getToken = [&message]() -> Token {
            return Token{ggapi::Struct{message.getPayload()}.get<std::string>("authToken")};
        };
        _connectedServiceName = getServiceNameFromToken(getToken());

        Message resp;
        resp.setType(AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_CONNECT_ACK);
        resp.setConnectionAccepted();
        _authenticated.store(true);
        sendProtocolMessage(resp);
    }

    void ServerConnection::onPing(const ipc_server::Message &message) {
        if(!_authenticated.load()) {
            // see onConnect()
            throw ggapi::GgApiError("Unexpected state");
        }
        Message resp;
        for(const auto &header : message.headers()) {
            if(!util::startsWith(header.name(), ":")) {
                // Headers not prefixed with ":" are round-trip
                resp.addHeader(header);
            }
        }
        resp.setPayloadAndContentType(message.getPayload());
        sendProtocolMessage(resp);
    }

    void ServerConnection::onPingResponse(const ipc_server::Message &) const {
        // GG-Interop: Ignore ping response
        // See amazon/awssdk/eventstreamrpc/ServiceOperationMappingContinuationHandler.java
        LOG.atWarn().kv("id", id()).log("Ignored Ping Response");
    }

    std::string ServerConnection::getServiceNameFromToken(const Token &authToken) const {
        auto name = IpcServer::getAuthHandler()->retrieveServiceName(authToken);
        if(name.has_value()) {
            return *std::move(name);
        } else {
            LOG.atError("authentication-error")
                .kv("id", id())
                .cause(ggapi::AccessDeniedError{})
                .log();
            // SECURITY-TODO: throw here instead of logging
            // throw ggapi::AccessDeniedError{};
            return {};
        }
    }

    std::string ServerConnection::getConnectedServiceName() {
        return _connectedServiceName;
    }

} // namespace ipc_server
