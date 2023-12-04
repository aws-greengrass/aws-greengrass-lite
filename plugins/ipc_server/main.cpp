#include "HeaderValue.hpp"

#include <aws/common/byte_order.h>
#include <aws/common/uuid.h>
#include <aws/crt/Api.h>
#include <aws/crt/Types.h>
#include <aws/crt/io/EventLoopGroup.h>
#include <aws/crt/io/SocketOptions.h>
#include <aws/event-stream/event_stream.h>
#include <aws/event-stream/event_stream_rpc_server.h>
#include <aws/io/channel_bootstrap.h>

#include <cpp_api.hpp>
#include <plugin.hpp>
#include <util.hpp>

#include <array>
#include <filesystem>
#include <optional>

#include <cstdio>
#include <cstring>

static void onListenerDestroy(aws_event_stream_rpc_server_listener *server, void *user_data);

//
// Connection Management
//
static int onNewServerConnection(
    aws_event_stream_rpc_server_connection *connection,
    int error_code,
    aws_event_stream_rpc_connection_options *connection_options,
    void *user_data);

static void onServerConnectionShutdown(
    aws_event_stream_rpc_server_connection *connection, int error_code, void *user_data);

static void onProtocolMessage(
    aws_event_stream_rpc_server_connection *connection,
    const aws_event_stream_rpc_message_args *message_args,
    void *user_data);

static void sendConnectionResponse(aws_event_stream_rpc_server_connection &connection);

//
// Stream Management
//
static int onIncomingStream(
    aws_event_stream_rpc_server_connection *connection,
    aws_event_stream_rpc_server_continuation_token *token,
    aws_byte_cursor operation_name,
    aws_event_stream_rpc_server_stream_continuation_options *continuation_options,
    void *user_data);

static void onContinuation(
    aws_event_stream_rpc_server_continuation_token *token,
    const aws_event_stream_rpc_message_args *message_args,
    void *user_data);

static void onContinuationClose(
    aws_event_stream_rpc_server_continuation_token *token, void *user_data);

//
// Messaging
//
template<class SendFn, size_t N, typename DataT = std::nullptr_t>
static int sendMessage(
    SendFn fn,
    std::array<aws_event_stream_header_value_pair, N> &headers,
    std::optional<ggapi::Buffer> payload,
    aws_event_stream_rpc_message_type message_type,
    uint32_t flags = 0);

static void onMessageFlush(int error_code, void *user_data);

//
// Operator Overloads
//

static std::ostream &operator<<(
    std::ostream &os, const aws_event_stream_rpc_message_args &message_args) {
    // print all headers and the payload
    using namespace std::string_view_literals;
    for(auto &&item : util::Span{message_args.headers, message_args.headers_count}) {
        auto &&[name, value] = parseHeader(item);
        if(name == ":message-type"sv
           && getIntOr(value, -1) == AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_CONNECT) {
            os << "Connect\n";
        }
        os << name << '=' << value << '\n';
    }
    auto sv = Aws::Crt::ByteCursorToStringView(aws_byte_cursor_from_buf(message_args.payload));
    return os.write(sv.data(), static_cast<std::streamsize>(sv.size())) << '\n';
}

//
// Class Definitions
//
class IpcServer final : public ggapi::Plugin {
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

    std::unordered_map<aws_event_stream_rpc_server_continuation_token *, std::string> continuations;

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
    aws_event_stream_rpc_server_listener *listener{};
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

bool IpcServer::onStart(ggapi::Struct data) {
    static constexpr std::string_view socket_path = "/tmp/gglite-ipc.socket";

    if(std::filesystem::exists(socket_path)) {
        std::filesystem::remove(socket_path);
    }

    aws_event_stream_rpc_server_listener_options listenerOptions = {
        .host_name = socket_path.data(),
        .port = port,
        .socket_options = &socketOpts.GetImpl(),
        .bootstrap = bootstrap,
        .on_new_connection = onNewServerConnection,
        .on_connection_shutdown = onServerConnectionShutdown,
        .on_destroy_callback = onListenerDestroy,
        .user_data = nullptr,
    };

    if(listener = aws_event_stream_rpc_server_new_listener(alloc, &listenerOptions); !listener) {
        int error_code = aws_last_error();
        throw std::runtime_error("Failed to create RPC server: " + std::to_string(error_code));
    }

    return true;
}

bool IpcServer::onTerminate(ggapi::Struct structData) {
    aws_event_stream_rpc_server_listener_release(listener);
    listener = nullptr;
    return true;
}

bool IpcServer::onBind(ggapi::Struct data) {
    return true;
}

static void onListenerDestroy(aws_event_stream_rpc_server_listener *server, void *user_data) {
    IpcServer::get().continuations.clear();
}

static int onNewServerConnection(
    aws_event_stream_rpc_server_connection *connection,
    int error_code,
    aws_event_stream_rpc_connection_options *connection_options,
    void *user_data) {
    std::ignore = connection;
    std::ignore = user_data;

    *connection_options = {
        .on_incoming_stream = onIncomingStream,
        .on_connection_protocol_message = onProtocolMessage};

    std::cerr << "[IPC] incoming connection with " << connection << '\n';
    return AWS_OP_SUCCESS;
}

static void onServerConnectionShutdown(
    aws_event_stream_rpc_server_connection *connection, int error_code, void *user_data) {
    std::cerr << "[IPC] connection closed with " << connection << " with error code " << error_code
              << '\n';
}

static void onProtocolMessage(
    aws_event_stream_rpc_server_connection *connection,
    const aws_event_stream_rpc_message_args *message_args,
    void *user_data) {
    std::cerr << "Received protocol message: " << *message_args << '\n';

    switch(message_args->message_type) {
        case AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_CONNECT:
            sendConnectionResponse(*connection);
            return;
        default:
            std::cerr << "Unhandled message type " << message_args->message_type << '\n';
            break;
    }
}

static void sendConnectionResponse(aws_event_stream_rpc_server_connection &connection) {
    std::array<aws_event_stream_header_value_pair, 3> headers{
        makeHeader(":message-type", int32_t{AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_CONNECT}),
        makeHeader(":message-flags", int32_t{0}),
        makeHeader(":stream-id", int32_t{0})};

    sendMessage(
        [connection = &connection](auto *args) {
            return aws_event_stream_rpc_server_connection_send_protocol_message(
                connection, args, onMessageFlush, nullptr);
        },
        headers,
        std::nullopt,
        AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_CONNECT_ACK,
        AWS_EVENT_STREAM_RPC_MESSAGE_FLAG_CONNECTION_ACCEPTED);
}

static int onIncomingStream(
    aws_event_stream_rpc_server_connection *connection,
    aws_event_stream_rpc_server_continuation_token *token,
    aws_byte_cursor operation_name,
    aws_event_stream_rpc_server_stream_continuation_options *continuation_options,
    void *user_data) {
    auto operation = [operation_name]() -> std::string {
        auto sv = Aws::Crt::ByteCursorToStringView(operation_name);
        return {sv.data(), sv.size()};
    }();

    std::cerr << "[IPC] Request for " << operation << " Received\n";

    IpcServer::get().continuations.emplace(token, "IPC::" + std::move(operation));
    *continuation_options = {
        .on_continuation = onContinuation, .on_continuation_closed = onContinuationClose};

    return AWS_OP_SUCCESS;
}

static void onContinuation(
    aws_event_stream_rpc_server_continuation_token *token,
    const aws_event_stream_rpc_message_args *message_args,
    void *user_data) {
    std::cerr << "[IPC] Continuation received:\n" << *message_args << '\n';

    if(message_args->message_flags & AWS_EVENT_STREAM_RPC_MESSAGE_FLAG_TERMINATE_STREAM) {
        std::cerr << "Stream terminating\n";
        return;
    }

    using namespace ggapi;

    auto jsonHandle =
        Buffer::create()
            .insert(-1, util::Span{message_args->payload->buffer, message_args->payload->len})
            .fromJson();

    auto json = jsonHandle.getHandleId() ? jsonHandle.unbox<Struct>() : Struct::create();

    std::string_view name = IpcServer::get().continuations.at(token);

    std::cerr << "[IPC] Publishing to LPC\n";
    auto response = Task::sendToTopic(name, json);
    std::cerr << "[IPC] LPC complete\n";

    auto getStreamHeader = [message_args] {
        for(auto &&header : util::Span{message_args->headers, message_args->headers_count}) {
            auto &&[name, value] = parseHeader(header);
            if(name == ":stream-id") {
                return header;
            }
        }
        return makeHeader(":stream-id", int32_t{0});
    };

    std::string contentType = "application/json";
    std::string serviceModel = std::string{name.substr(sizeof("IPC::") - 1)} + "Response";
    std::array headers{
        makeHeader(":message-type", int32_t{AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_APPLICATION_MESSAGE}),
        makeHeader(":message-flags", int32_t{0}),
        getStreamHeader(),
        makeHeader("service-model-type", stringbuffer{serviceModel}),
        makeHeader(":content-type", stringbuffer(contentType))};

    sendMessage(
        [token](auto *args) {
            return aws_event_stream_rpc_server_continuation_send_message(
                token, args, onMessageFlush, nullptr);
        },
        headers,
        response.getHandleId() ? std::make_optional(response.toJson()) : std::nullopt,
        aws_event_stream_rpc_message_type::AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_APPLICATION_MESSAGE);
}

static void onContinuationClose(
    aws_event_stream_rpc_server_continuation_token *token, void *user_data) {
    IpcServer::get().continuations.erase(token);
}

template<class SendFn, size_t N, typename DataT>
static int sendMessage(
    SendFn fn,
    std::array<aws_event_stream_header_value_pair, N> &headers,
    std::optional<ggapi::Buffer> payload,
    aws_event_stream_rpc_message_type message_type,
    uint32_t flags) {
    aws_array_list headers_list{
        .alloc = nullptr,
        .current_size = util::Span{headers}.size_bytes(),
        .length = std::size(headers),
        .item_size = sizeof(aws_event_stream_header_value_pair),
        .data = std::data(headers),
    };

    Aws::Crt::Vector<uint8_t> payloadVec({'{', '}'});
    if(payload.has_value()) {
        payloadVec = payload->get<Aws::Crt::Vector<uint8_t>>(
            0, std::min(payload->size(), uint32_t{AWS_EVENT_STREAM_MAX_MESSAGE_SIZE}));
    }
    auto payloadBytes = Aws::Crt::ByteBufFromArray(payloadVec.data(), payloadVec.size());
    aws_event_stream_message message{};
    aws_event_stream_message_init(&message, Aws::Crt::ApiAllocator(), &headers_list, &payloadBytes);

    aws_event_stream_rpc_message_args args = {
        .headers = std::data(headers),
        .headers_count = std::size(headers),
        .payload = &payloadBytes,
        .message_type = message_type,
        .message_flags = flags,
    };

    return fn(&args);
}

static void onMessageFlush(int error_code, void *user_data) {
    std::ignore = user_data;
    std::ignore = error_code;
}
