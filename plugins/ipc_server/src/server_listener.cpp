#include "server_listener.hpp"
#include <temp_module.hpp>

void ServerListener::Connect(std::string_view socket_path) {
    // TODO: This should be refactored again into a new class
    if(std::filesystem::exists(socket_path)) {
        std::filesystem::remove(socket_path);
    }

    aws_event_stream_rpc_server_listener_options listenerOptions = {};
    listenerOptions.host_name = socket_path.data();
    listenerOptions.port = port;
    listenerOptions.socket_options = &_socketOpts.GetImpl();
    listenerOptions.bootstrap = _bootstrap.GetUnderlyingHandle();
    listenerOptions.on_new_connection = ServerListenerCCallbacks::onNewServerConnection;
    listenerOptions.on_connection_shutdown = ServerListenerCCallbacks::onServerConnectionShutdown;
    listenerOptions.on_destroy_callback = onListenerDestroy;
    listenerOptions.user_data = static_cast<void *>(this);

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
        [conn](auto *args) {
            return aws_event_stream_rpc_server_connection_send_protocol_message(
                conn, args, onMessageFlush, nullptr);
        },
        AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_CONNECT_ACK,
        AWS_EVENT_STREAM_RPC_MESSAGE_FLAG_CONNECTION_ACCEPTED);
}

int ServerListener::sendPingResponse(Connection *conn) {
    return sendMessage(
        [conn](auto *args) {
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
        [conn](auto *args) {
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
    util::TempModule tempModule{thisConnection->module()};

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

    auto *thisConnection = static_cast<ServerListener *>(user_data);
    util::TempModule tempModule{thisConnection->module()};
    const std::scoped_lock<std::recursive_mutex> lock{thisConnection->stateMutex};

    thisConnection->underlyingConnection.remove(connection);

    if(error_code == 1051) {
        std::cerr << "[IPC] connection closed with " << connection << " successfully ("
                  << error_code << ")" << std::endl;
    } else {
        std::cerr << "[IPC] connection closed with " << connection << " with error code "
                  << error_code << std::endl;
    }
}

void ServerListenerCCallbacks::onProtocolMessage(
    aws_event_stream_rpc_server_connection *connection,
    const aws_event_stream_rpc_message_args *message_args,
    void *user_data) noexcept {
    auto *thisConnection = static_cast<ServerListener *>(user_data);
    util::TempModule tempModule{thisConnection->module()};
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
    aws_event_stream_rpc_server_connection *,
    aws_event_stream_rpc_server_continuation_token *token,
    aws_byte_cursor operation_name,
    aws_event_stream_rpc_server_stream_continuation_options *continuation_options,
    void *user_data) noexcept {

    auto *thisConnection = static_cast<ServerListener *>(user_data);
    util::TempModule tempModule{thisConnection->module()};

    auto operationName = [operation_name]() -> std::string {
        auto sv = Aws::Crt::ByteCursorToStringView(operation_name);
        return {sv.data(), sv.size()};
    }();

    std::cerr << "[IPC] Request for " << operationName << " Received\n";

    auto *continuation = new std::shared_ptr{
        std::make_shared<ServerContinuation>(*tempModule, token, std::move(operationName))};

    *continuation_options = {};
    continuation_options->on_continuation = ServerContinuationCCallbacks::onContinuation;
    continuation_options->on_continuation_closed =
        ServerContinuationCCallbacks::onContinuationClose;
    // NOLINTNEXTLINE
    continuation_options->user_data = static_cast<void *>(continuation);

    return AWS_OP_SUCCESS;
}
}
