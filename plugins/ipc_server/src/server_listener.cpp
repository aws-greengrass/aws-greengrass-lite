#include "server_listener.hpp"

void ServerListener::Connect(std::string_view socket_path) {
    // TODO: This should be refactored again into a new class
    if(std::filesystem::exists(socket_path)) {
        std::filesystem::remove(socket_path);
    }

    aws_event_stream_rpc_server_listener_options listenerOptions = {
        .host_name = socket_path.data(),
        .port = port,
        .socket_options = &_socketOpts.GetImpl(),
        .bootstrap = _bootstrap.GetUnderlyingHandle(),
        .on_new_connection = ServerListenerCCallbacks::onNewServerConnection,
        .on_connection_shutdown = ServerListenerCCallbacks::onServerConnectionShutdown,
        .on_destroy_callback = onListenerDestroy,
        .user_data = static_cast<void *>(this),
    };

    if(listener =
           aws_event_stream_rpc_server_new_listener(Aws::Crt::ApiAllocator(), &listenerOptions);
       !listener) {
        int error_code = aws_last_error();
        throw std::runtime_error{"Failed to create RPC server: " + std::to_string(error_code)};
    }
}

void ServerListener::Disconnect() {
    aws_event_stream_rpc_server_listener_release(std::exchange(listener, nullptr));
}

void ServerListener::Close(int shutdownCode) noexcept {
    std::unique_lock guard{stateMutex};
    aws_event_stream_rpc_server_listener_release(listener);
}

int ServerListener::sendConnectionResponse(Connection *conn) {
    return sendMessage(
        [this, conn](auto *args) {
            return aws_event_stream_rpc_server_connection_send_protocol_message(
                conn, args, onMessageFlush, nullptr);
        },
        AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_CONNECT_ACK,
        AWS_EVENT_STREAM_RPC_MESSAGE_FLAG_CONNECTION_ACCEPTED);
}

int ServerListener::sendPingResponse(Connection *conn) {
    return sendMessage(
        [this, conn](auto *args) {
            return aws_event_stream_rpc_server_connection_send_protocol_message(
                conn, args, onMessageFlush, nullptr);
        },
        AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_PING_RESPONSE,
        0);
}

int ServerListener::sendErrorResponse(
    Connection *conn,
    std::string_view message,
    aws_event_stream_rpc_message_type error_type,
    uint32_t flags) {
    ggapi::Buffer payload = ggapi::Buffer::create().put(0, message);
    std::array headers{
        makeHeader(Headers::ContentType, Headervaluetypes::stringbuffer(ContentType::JSON))};
    return sendMessage(
        [this, conn](auto *args) {
            return aws_event_stream_rpc_server_connection_send_protocol_message(
                conn, args, onMessageFlush, nullptr);
        },
        headers,
        payload,
        error_type,
        flags);
}

extern "C" {
int ServerListenerCCallbacks::onNewServerConnection(
    aws_event_stream_rpc_server_connection *connection,
    int error_code,
    aws_event_stream_rpc_connection_options *connection_options,
    void *user_data) noexcept {

    auto *thisConnection = static_cast<ServerListener *>(user_data);

    const std::scoped_lock<std::recursive_mutex> lock{thisConnection->stateMutex};
    if(error_code) {
        aws_event_stream_rpc_server_connection_release(connection);
        return AWS_OP_ERR;
    } else {
        thisConnection->underlyingConnection.push_front(connection);

        *connection_options = {
            onIncomingStream,
            onProtocolMessage,
            // NOLINTNEXTLINE
            user_data};

        std::cerr << "[IPC] incoming connection\n";
        return AWS_OP_SUCCESS;
    }
}

void ServerListenerCCallbacks::onServerConnectionShutdown(
    aws_event_stream_rpc_server_connection *connection, int error_code, void *user_data) noexcept {
    (void) connection;
    auto *thisConnection = static_cast<ServerListener *>(user_data);
    const std::scoped_lock<std::recursive_mutex> lock{thisConnection->stateMutex};

    thisConnection->underlyingConnection.remove(connection);
    std::cerr << "[IPC] connection closed with " << connection << " with error code " << error_code
              << '\n';
}

void ServerListenerCCallbacks::onProtocolMessage(
    aws_event_stream_rpc_server_connection *connection,
    const aws_event_stream_rpc_message_args *message_args,
    void *user_data) noexcept {
    auto *thisConnection = static_cast<ServerListener *>(user_data);
    const std::scoped_lock<std::recursive_mutex> lock{thisConnection->stateMutex};

    std::cerr << "Received protocol message: " << *message_args << '\n';

    // TODO:  Authentication handler based on auth token

    switch(message_args->message_type) {
        case AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_CONNECT:
            thisConnection->sendConnectionResponse(connection);
            return;
        case AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_PING:
            thisConnection->sendPingResponse(connection);
            return;
        case AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_PING_RESPONSE:
            // GG-Java Interop
            return;
        default:
            std::cerr << "Unhandled message type " << message_args->message_type << '\n';
            std::string messageValue = std::to_string(
                static_cast<std::underlying_type_t<aws_event_stream_rpc_message_type>>(
                    message_args->message_type));
            std::string bufMessage =
                R"({ "error": "Unrecognized Message Type", "message": " message type value: )"
                + messageValue + " is not recognized as a valid request path.\" }";
            thisConnection->sendErrorResponse(
                connection, bufMessage, AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_INTERNAL_ERROR, 0);
            break;
    }
}

int ServerListenerCCallbacks::onIncomingStream(
    aws_event_stream_rpc_server_connection *connection,
    aws_event_stream_rpc_server_continuation_token *token,
    aws_byte_cursor operation_name,
    aws_event_stream_rpc_server_stream_continuation_options *continuation_options,
    void *user_data) noexcept {
    auto operationName = [operation_name]() -> std::string {
        auto sv = Aws::Crt::ByteCursorToStringView(operation_name);
        return {sv.data(), sv.size()};
    }();

    std::cerr << "[IPC] Request for " << operationName << " Received\n";

    auto *continuation =
        new std::shared_ptr{std::make_shared<ServerContinuation>(token, std::move(operationName))};

    *continuation_options = {
        .on_continuation = ServerContinuationCCallbacks::onContinuation,
        .on_continuation_closed = ServerContinuationCCallbacks::onContinuationClose,
        // NOLINTNEXTLINE
        .user_data = static_cast<void *>(continuation)};

    return AWS_OP_SUCCESS;
}
}
