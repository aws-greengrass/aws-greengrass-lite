#include "HeaderValue.hpp"

#include <forward_list>
#include <limits>
#include <optional>
#include <plugin.hpp>

#include "ServerBootstrap.hpp"
#include "cpp_api.hpp"
#include <aws/crt/Api.h>
#include <aws/crt/Types.h>
#include <aws/crt/io/EventLoopGroup.h>
#include <aws/crt/io/SocketOptions.h>
#include <aws/event-stream/event_stream_rpc_server.h>

#include <filesystem>

class Listener;

struct Keys {
private:
    Keys() = default;

public:
    ggapi::Symbol terminate{"terminate"};
    ggapi::Symbol contentType{"contentType"};
    ggapi::Symbol serviceModelType{"serviceModelType"};
    ggapi::Symbol shape{"shape"};
    ggapi::Symbol accepted{"accepted"};
    ggapi::Symbol errorCode{"errorCode"};
    ggapi::Symbol channel{"channel"};

    static const Keys &get() {
        static Keys keys;
        return keys;
    }
};

static void onListenerDestroy(
    aws_event_stream_rpc_server_listener *server, void *user_data) noexcept;

//
// Messaging
//

template<class SendFn>
static int sendMessage(
    SendFn fn,
    util::Span<aws_event_stream_header_value_pair> headers,
    ggapi::Buffer payload,
    aws_event_stream_rpc_message_type message_type,
    uint32_t flags = 0);

template<class SendFn>
static int sendMessage(
    SendFn fn, aws_event_stream_rpc_message_type message_type, uint32_t flags = 0);

static void onMessageFlush(int error_code, void *user_data) noexcept;

//
// Operator Overloads
//

static std::ostream &operator<<(
    std::ostream &os, const aws_event_stream_rpc_message_args &message_args) {
    // print all headers and the payload
    using namespace std::string_view_literals;
    for(auto &&item : util::Span{message_args.headers, message_args.headers_count}) {
        auto &&[name, value] = parseHeader(item);
        os << name << '=';
        if(value) {
            os << *value;
        } else {
            os << "unsupported header_value_type: " << item.header_value_type;
        }
        os << '\n';
    }
    auto sv = Aws::Crt::ByteCursorToStringView(aws_byte_cursor_from_buf(message_args.payload));
    return os.write(sv.data(), static_cast<std::streamsize>(sv.size()));
}

//
// Class Definitions
//

class IpcServer final : public ggapi::Plugin {
private:
    using MutexType = std::shared_mutex;
    template<template<class> class Lock>
    static constexpr bool is_lockable = std::is_constructible_v<Lock<MutexType>, MutexType &>;

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

    template<template<class> class Lock = std::unique_lock>
    std::enable_if_t<is_lockable<Lock>, Lock<MutexType>> lock() & {
        return Lock{mutex};
    }

private:
    MutexType mutex;
    std::shared_ptr<Listener> _listener;
};

class ServerContinuation {
public:
    using Token = aws_event_stream_rpc_server_continuation_token;

private:
    Token *_token;
    std::string _operation;
    ggapi::Channel _channel{};
    using ContinutationHandle = std::shared_ptr<ServerContinuation> *;

public:
    explicit ServerContinuation(Token *token, std::string operation)
        : _token{token}, _operation{std::move(operation)} {
    }

    ~ServerContinuation() noexcept {
        if(_channel) {
            _channel.close();
            _channel.release();
        }
    }

    Token *GetUnderlyingHandle() {
        return _token;
    }

    [[nodiscard]] std::string lpcTopic() const {
        return "IPC::" + _operation;
    }

    [[nodiscard]] std::string ipcServiceModel() const {
        return _operation + "Response";
    }

    static ggapi::Struct onTopicResponse(
        const std::weak_ptr<ServerContinuation> &weakSelf, ggapi::Struct response) {
        // TODO: unsubscribe
        auto self = weakSelf.lock();
        if(!self) {
            return ggapi::Struct::create();
        }

        const auto &keys = Keys::get();

        auto messageType = response.hasKey(keys.errorCode) && response.get<int>(keys.errorCode) != 0
                               ? AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_APPLICATION_ERROR
                               : AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_APPLICATION_MESSAGE;

        int32_t flags = (response.hasKey(keys.terminate) && response.get<bool>(keys.terminate))
                            ? AWS_EVENT_STREAM_RPC_MESSAGE_FLAG_TERMINATE_STREAM
                            : 0;

        auto json = response.hasKey(keys.shape) ? response.get<ggapi::Struct>(keys.shape).toJson()
                                                : ggapi::Struct::create().toJson();

        auto serviceModel = response.get<std::string>(keys.serviceModelType);
        if(serviceModel.size() > std::numeric_limits<uint16_t>::max()) {
            // TODO: Error handling
            serviceModel.resize(std::numeric_limits<uint16_t>::max());
        }

        const auto sender = [self = std::move(self)](auto *args) {
            return aws_event_stream_rpc_server_continuation_send_message(
                self->GetUnderlyingHandle(), args, onMessageFlush, nullptr);
        };

        using namespace std::string_literals;
        auto contentType = response.hasKey(keys.contentType)
                               ? response.get<std::string>(keys.contentType)
                               : ContentType::JSON;
        if(contentType.size() > std::numeric_limits<uint16_t>::max()) {
            // TODO: Error handling
            contentType.resize(std::numeric_limits<uint16_t>::max());
        }

        std::array headers{
            makeHeader(Headers::ServiceModelType, Headervaluetypes::stringbuffer{serviceModel}),
            makeHeader(Headers::ContentType, Headervaluetypes::stringbuffer(contentType))};
        int result = sendMessage(sender, headers, json, messageType, flags);
        if(result != AWS_OP_SUCCESS) {
            ggapi::Buffer payload =
                ggapi::Buffer::create().put(0, std::string_view{"InternalServerError"});
            std::array errorHeaders{makeHeader(
                Headers::ContentType, Headervaluetypes::stringbuffer(ContentType::Text))};
            sendMessage(
                sender,
                headers,
                payload,
                AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_APPLICATION_ERROR,
                AWS_EVENT_STREAM_RPC_MESSAGE_FLAG_TERMINATE_STREAM);
        }
        return ggapi::Struct::create();
    }

    static void onContinuation(
        aws_event_stream_rpc_server_continuation_token *token,
        const aws_event_stream_rpc_message_args *message_args,
        void *user_data) noexcept {
        std::cerr << "[IPC] Continuation received:\n" << *message_args << '\n';

        if(message_args->message_flags & AWS_EVENT_STREAM_RPC_MESSAGE_FLAG_TERMINATE_STREAM) {
            std::cerr << "Stream terminating\n";
            return;
        }

        using namespace ggapi;

        auto json = [message_args] {
            auto jsonHandle =
                Buffer::create()
                    .insert(
                        -1, util::Span{message_args->payload->buffer, message_args->payload->len})
                    .fromJson();
            return jsonHandle.getHandleId() ? jsonHandle.unbox<Struct>() : Struct::create();
        }();
        auto const &keys = Keys::get();
        auto scope = ggapi::CallScope{};
        // NOLINTNEXTLINE
        auto continuation = *static_cast<ContinutationHandle>(user_data);
        auto response = Task::sendToTopic(continuation->lpcTopic(), json);
        if(response.empty()) {
            std::cerr << "[IPC] LPC appears unhandled\n";
            const auto sender = [continuation](auto *args) {
                return aws_event_stream_rpc_server_continuation_send_message(
                    continuation->GetUnderlyingHandle(), args, onMessageFlush, nullptr);
            };
            std::string message = R"({ "error": "LPC unhandled", "message": ")"
                                  "LPC unhandled.\" }";
            ggapi::Buffer payload = ggapi::Buffer::create().put(0, std::string_view{message});
            std::array headers{makeHeader(
                Headers::ContentType, Headervaluetypes::stringbuffer(ContentType::JSON))};
            sendMessage(
                sender,
                headers,
                payload,
                AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_APPLICATION_ERROR,
                AWS_EVENT_STREAM_RPC_MESSAGE_FLAG_TERMINATE_STREAM);
        } else {
            response.put(keys.serviceModelType, continuation->ipcServiceModel());
            ServerContinuation::onTopicResponse(continuation, response);
            if(response.hasKey(keys.channel)) {
                auto channel =
                    IpcServer::get().getScope().anchor(response.get<ggapi::Channel>(keys.channel));
                continuation->_channel = channel;
                channel.addListenCallback(ggapi::ChannelListenCallback::of(
                    &ServerContinuation::onTopicResponse, std::weak_ptr{continuation}));
            }
        }
    }

    static void onContinuationClose(
        aws_event_stream_rpc_server_continuation_token *token, void *user_data) noexcept {
        // NOLINTNEXTLINE
        auto continuation = static_cast<ContinutationHandle>(user_data);
        std::cerr << "Stream ending for " << (*continuation)->_operation;
        // NOLINTNEXTLINE
        delete continuation;
    }
};

class Listener {
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
    Listener(const Listener &) = delete;
    Listener(Listener &&) = delete;
    Listener &operator=(const Listener &) = delete;
    Listener &operator=(Listener &&) = delete;
    explicit Listener(Aws::Crt::Allocator *allocator = Aws::Crt::g_allocator)
        : _allocator(allocator), _eventLoop(1, allocator), _bootstrap(_eventLoop, allocator) {
    }

    ~Listener() noexcept {
        Close();
    }

    void Connect() {
        // TODO: This should be refactored again into a new class
        static constexpr std::string_view socket_path = "/tmp/gglite-ipc.socket";

        if(std::filesystem::exists(socket_path)) {
            std::filesystem::remove(socket_path);
        }

        aws_event_stream_rpc_server_listener_options listenerOptions = {
            .host_name = socket_path.data(),
            .port = port,
            .socket_options = &_socketOpts.GetImpl(),
            .bootstrap = _bootstrap.GetUnderlyingHandle(),
            .on_new_connection = onNewServerConnection,
            .on_connection_shutdown = onServerConnectionShutdown,
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

    void Disconnect() {
        aws_event_stream_rpc_server_listener_release(std::exchange(listener, nullptr));
    }

    void Close(int shutdownCode = AWS_ERROR_SUCCESS) noexcept {
        std::unique_lock guard{stateMutex};
        aws_event_stream_rpc_server_listener_release(listener);
    }

    int sendConnectionResponse(Connection *conn) {
        return sendMessage(
            [this, conn](auto *args) {
                return aws_event_stream_rpc_server_connection_send_protocol_message(
                    conn, args, onMessageFlush, nullptr);
            },
            AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_CONNECT_ACK,
            AWS_EVENT_STREAM_RPC_MESSAGE_FLAG_CONNECTION_ACCEPTED);
    }

    int sendPingResponse(Connection *conn) {
        return sendMessage(
            [this, conn](auto *args) {
                return aws_event_stream_rpc_server_connection_send_protocol_message(
                    conn, args, onMessageFlush, nullptr);
            },
            AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_PING_RESPONSE,
            0);
    }

    int sendErrorResponse(
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

    static int onNewServerConnection(
        aws_event_stream_rpc_server_connection *connection,
        int error_code,
        aws_event_stream_rpc_connection_options *connection_options,
        void *user_data) noexcept {

        auto *thisConnection = static_cast<Listener *>(user_data);

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

    static void onServerConnectionShutdown(
        aws_event_stream_rpc_server_connection *connection,
        int error_code,
        void *user_data) noexcept {
        (void) connection;
        auto *thisConnection = static_cast<Listener *>(user_data);
        const std::scoped_lock<std::recursive_mutex> lock{thisConnection->stateMutex};

        thisConnection->underlyingConnection.remove(connection);
        std::cerr << "[IPC] connection closed with " << connection << " with error code "
                  << error_code << '\n';
    }

    static void onProtocolMessage(
        aws_event_stream_rpc_server_connection *connection,
        const aws_event_stream_rpc_message_args *message_args,
        void *user_data) noexcept {
        auto *thisConnection = static_cast<Listener *>(user_data);
        const std::scoped_lock<std::recursive_mutex> lock{thisConnection->stateMutex};

        std::cerr << "Received protocol message: " << *message_args << '\n';

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

    static int onIncomingStream(
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

        auto *continuation = new std::shared_ptr{
            std::make_shared<ServerContinuation>(token, std::move(operationName))};

        *continuation_options = {
            .on_continuation = ServerContinuation::onContinuation,
            .on_continuation_closed = ServerContinuation::onContinuationClose,
            // NOLINTNEXTLINE
            .user_data = static_cast<void *>(continuation)};

        return AWS_OP_SUCCESS;
    }
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
    _listener = std::make_shared<Listener>();
    try {
        _listener->Connect();
    } catch(std::runtime_error &e) {
        throw ggapi::GgApiError(e.what());
    }
    return true;
}

bool IpcServer::onTerminate(ggapi::Struct structData) {
    _listener->Disconnect();
    return true;
}

bool IpcServer::onBind(ggapi::Struct data) {
    return true;
}

static void onListenerDestroy(
    aws_event_stream_rpc_server_listener *server, void *user_data) noexcept {
}

template<class SendFn>
static int sendMessage(SendFn fn, aws_event_stream_rpc_message_type message_type, uint32_t flags) {
    auto payload = Aws::Crt::ByteBufFromEmptyArray(nullptr, 0);

    aws_event_stream_rpc_message_args args = {
        .headers = nullptr,
        .headers_count = 0,
        .payload = &payload,
        .message_type = message_type,
        .message_flags = flags,
    };

    std::cerr << "Sending message:\n" << args << '\n';

    return fn(&args);
}

template<class SendFn>
static int sendMessage(
    SendFn fn,
    util::Span<aws_event_stream_header_value_pair> headers,
    ggapi::Buffer payload,
    aws_event_stream_rpc_message_type message_type,
    uint32_t flags) {
    aws_array_list headers_list{
        .alloc = nullptr,
        .current_size = headers.size_bytes(),
        .length = std::size(headers),
        .item_size = sizeof(aws_event_stream_header_value_pair),
        .data = std::data(headers),
    };

    auto payloadVec = payload.get<Aws::Crt::Vector<uint8_t>>(
        0, std::min(payload.size(), uint32_t{AWS_EVENT_STREAM_MAX_MESSAGE_SIZE}));
    auto payloadBytes = Aws::Crt::ByteBufFromArray(payloadVec.data(), payloadVec.size());

    aws_event_stream_rpc_message_args args = {
        .headers = std::data(headers),
        .headers_count = std::size(headers),
        .payload = &payloadBytes,
        .message_type = message_type,
        .message_flags = flags,
    };
    std::cerr << "Sending message:\n" << args << '\n';

    return fn(&args);
}

static void onMessageFlush(int error_code, void *user_data) noexcept {
    std::ignore = user_data;
    std::ignore = error_code;
}
