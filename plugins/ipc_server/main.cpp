#include <cpp_api.hpp>
#include <plugin.hpp>
#include <util.hpp>

#include <algorithm>
#include <aws/common/array_list.h>
#include <aws/common/byte_buf.h>
#include <aws/common/uuid.h>
#include <aws/crt/Allocator.h>
#include <aws/crt/Api.h>
#include <aws/crt/DateTime.h>
#include <aws/crt/JsonObject.h>
#include <aws/crt/StlAllocator.h>
#include <aws/crt/io/EventLoopGroup.h>
#include <aws/crt/io/SocketOptions.h>
#include <aws/event-stream/event_stream.h>
#include <aws/event-stream/event_stream_rpc.h>
#include <aws/event-stream/event_stream_rpc_server.h>
#include <aws/http/request_response.h>
#include <aws/io/channel_bootstrap.h>

#include <chrono>
#include <filesystem>
#include <future>
#include <ios>
#include <iostream>
#include <ostream>
#include <stdexcept>
#include <streambuf>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>

#include <cstdio>
#include <cstring>
#include <variant>

using timestamp = std::chrono::duration<uint32_t, std::milli>;
using bytebuffer = util::Span<uint8_t, uint16_t>;
using stringbuffer = util::Span<char, uint16_t>;

using HeaderValue = std::variant<
    bool,
    uint8_t,
    int16_t,
    int32_t,
    int64_t,
    bytebuffer,
    stringbuffer,
    timestamp,
    aws_uuid>;

template<typename T>
constexpr bool is_static_value = std::is_arithmetic_v<T> || std::is_same_v<timestamp, T>
                                 || std::is_same_v<aws_uuid, T> || std::is_same_v<T, bool>;
template<typename T>
constexpr bool is_dynamic_value = !is_static_value<T>;

template<typename To, size_t N>
// NOLINTNEXTLINE(*-c-arrays)
To from_network_bytes(const uint8_t (&buffer)[N]) noexcept {
    static_assert(N >= sizeof(To));
    static_assert(std::is_trivially_default_constructible_v<To>);
    static_assert(std::is_trivially_copy_assignable_v<To>);

    To to{};
    if(aws_is_big_endian()) {
        std::memcpy(&to, std::data(buffer), sizeof(To));
        return to;
    } else {
        // convert from network byte order
        auto *punned = reinterpret_cast<uint8_t *>(&to);
        std::reverse_copy(std::begin(buffer), std::next(std::begin(buffer), sizeof(To)), punned);
    }
    return to;
}

template<size_t N, typename From>
// NOLINTNEXTLINE(*-c-arrays)
void to_network_bytes(uint8_t (&buffer)[N], const From &from) noexcept {
    static_assert(N >= sizeof(From));
    static_assert(std::is_trivially_copy_assignable_v<From>);
    if(aws_is_big_endian()) {
        std::memcpy(std::data(buffer), &from, sizeof(From));
    } else {
        auto *punned = reinterpret_cast<const uint8_t *>(&from);
        std::reverse_copy(punned, std::next(punned, sizeof(From)), std::begin(buffer));
    }
}

// NOLINTBEGIN(*-union-access)
HeaderValue getValue(const aws_event_stream_header_value_pair &header) noexcept {
    auto *staticData = std::data(header.header_value.static_val);
    auto *variableData = header.header_value.variable_len_val;
    switch(header.header_value_type) {
        case AWS_EVENT_STREAM_HEADER_BOOL_TRUE:
            return true;
        case AWS_EVENT_STREAM_HEADER_BOOL_FALSE:
            return false;
        case AWS_EVENT_STREAM_HEADER_BYTE:
            return from_network_bytes<uint8_t>(header.header_value.static_val);
        case AWS_EVENT_STREAM_HEADER_INT16:
            return from_network_bytes<int16_t>(header.header_value.static_val);
        case AWS_EVENT_STREAM_HEADER_INT32:
            return from_network_bytes<int32_t>(header.header_value.static_val);
        case AWS_EVENT_STREAM_HEADER_INT64:
            return from_network_bytes<int64_t>(header.header_value.static_val);
        case AWS_EVENT_STREAM_HEADER_BYTE_BUF:
            return bytebuffer{variableData, header.header_value_len};
        case AWS_EVENT_STREAM_HEADER_STRING:
            return stringbuffer{// NOLINTNEXTLINE(*-reinterpret-cast)
                                reinterpret_cast<char *>(variableData),
                                header.header_value_len};
        case AWS_EVENT_STREAM_HEADER_TIMESTAMP:
            return timestamp{from_network_bytes<uint32_t>(header.header_value.static_val)};
        case AWS_EVENT_STREAM_HEADER_UUID: {
            return from_network_bytes<aws_uuid>(header.header_value.static_val);
        }
    }
}

static inline aws_event_stream_header_value_type getType(const HeaderValue &variant) {
    return std::visit(
        [](auto &&value) -> aws_event_stream_header_value_type {
            using T = std::decay_t<decltype(value)>;
            if constexpr(std::is_same_v<bool, T>) {
                return value ? AWS_EVENT_STREAM_HEADER_BOOL_TRUE
                             : AWS_EVENT_STREAM_HEADER_BOOL_FALSE;
            } else if constexpr(std::is_same_v<uint8_t, T>) {
                return AWS_EVENT_STREAM_HEADER_BYTE;
            } else if constexpr(std::is_same_v<int16_t, T>) {
                return AWS_EVENT_STREAM_HEADER_INT16;
            } else if constexpr(std::is_same_v<int32_t, T>) {
                return AWS_EVENT_STREAM_HEADER_INT32;
            } else if constexpr(std::is_same_v<int64_t, T>) {
                return AWS_EVENT_STREAM_HEADER_INT64;
            } else if constexpr(std::is_same_v<bytebuffer, T>) {
                return AWS_EVENT_STREAM_HEADER_BYTE_BUF;
            } else if constexpr(std::is_same_v<stringbuffer, T>) {
                return AWS_EVENT_STREAM_HEADER_STRING;
            } else if constexpr(std::is_same_v<timestamp, T>) {
                return AWS_EVENT_STREAM_HEADER_TIMESTAMP;
            } else if constexpr(std::is_same_v<aws_uuid, T>) {
                return AWS_EVENT_STREAM_HEADER_UUID;
            }
        },
        variant);
}

static aws_event_stream_header_value_pair makeHeader(std::string_view name, HeaderValue var) {
    return std::visit(
        [&var, name](auto &&val) {
            aws_event_stream_header_value_pair args{};

            using T = std::decay_t<decltype(val)>;
            if constexpr(is_static_value<T>) {
                std::memcpy(std::data(args.header_value.static_val), &val, sizeof(val));
                args.header_value_len = sizeof(val);
            } else { // span or string view
                args.header_value.variable_len_val = reinterpret_cast<uint8_t *>(std::data(val));
                args.header_value_len = std::size(val);

                if constexpr(std::is_same_v<T, stringbuffer>) { // string
                    args.header_value_type = AWS_EVENT_STREAM_HEADER_STRING;
                } else { // buffer
                    args.header_value_type = AWS_EVENT_STREAM_HEADER_BYTE_BUF;
                }
            }

            args.header_value_type = getType(var);

            return args;
        },
        var);
}
// NOLINTEND(*-union-access)

static std::ostream &operator<<(std::ostream &os, HeaderValue v) {
    return std::visit(
        [&os](auto &&val) -> std::ostream & {
            using T = std::decay_t<decltype(val)>;
            if constexpr(std::is_arithmetic_v<T> || std::is_same_v<std::string_view, T>) {
                os << val;
            } else if constexpr(
                std::is_same_v<std::true_type, T> || std::is_same_v<std::false_type, T>) {
                auto flags = os.flags();
                os.flags(std::ios::boolalpha);
                os << static_cast<bool>(val);
                os.flags(flags);
            } else if constexpr(std::is_same_v<timestamp, T>) {
                os << val.count() << "ms";
            } else if constexpr(std::is_same_v<util::Span<uint8_t, uint16_t>, T>) {
                os.write(reinterpret_cast<const char *>(val.begin()), val.size());
            } else if constexpr(std::is_same_v<T, aws_uuid>) {
                auto flags = os.flags();
                os.flags(std::ios::hex | std::ios::uppercase);
                for(auto &&v : val.uuid_data) {
                    os << v;
                }
                os.flags(flags);
            }
            return os;
        },
        v);
}

static auto parseHeader(const aws_event_stream_header_value_pair &pair) {
    return std::make_pair(
        std::string_view{std::data(pair.header_name), pair.header_name_len}, getValue(pair));
}

template<typename IntT = int64_t>
static IntT getIntOr(const HeaderValue &var, IntT &&alternative = IntT{0}) {
    return std::visit(
        [&alternative](auto &&var) -> IntT {
            using T = std::decay_t<decltype(var)>;
            if constexpr(std::is_arithmetic_v<T>) {
                return var;
            } else {
                return std::forward<IntT>(alternative);
            }
        },
        var);
}

static void s_on_message_flush_fn(int error_code, void *user_data) {
    static thread_local uintmax_t counter = 0;
    std::cerr << "Message " << counter << " flushed: " << error_code << std::endl;
    std::ignore = user_data;
}

static void doConnection(aws_event_stream_rpc_server_connection &connection) {
    std::array<aws_event_stream_header_value_pair, 3> headers{
        makeHeader(":message-type", int32_t{AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_CONNECT}),
        makeHeader(":message-flags", int32_t{0}),
        makeHeader(":stream-id", int32_t{0})};

    aws_array_list headers_list{};
    aws_event_stream_headers_list_init(&headers_list, Aws::Crt::ApiAllocator());
    for(auto &&header : headers) {
        aws_array_list_push_back(&headers_list, &header);
    }

    aws_byte_buf payload{aws_byte_buf_from_c_str("")};
    aws_event_stream_message message{};
    aws_event_stream_message_init(&message, Aws::Crt::ApiAllocator(), &headers_list, &payload);

    struct aws_event_stream_rpc_message_args connect_ack_args = {
        .payload = &payload,
        .message_type = AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_CONNECT_ACK,
        .message_flags = AWS_EVENT_STREAM_RPC_MESSAGE_FLAG_CONNECTION_ACCEPTED,
    };

    aws_event_stream_rpc_server_connection_send_protocol_message(
        &connection, &connect_ack_args, s_on_message_flush_fn, nullptr);

    if(aws_array_list_is_valid(&headers_list)) {
        aws_array_list_clean_up(&headers_list);
    }
    aws_event_stream_message_clean_up(&message);
}

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
    std::string_view payload(
        reinterpret_cast<const char *>(message_args.payload->buffer), message_args.payload->len);
    return os << payload << '\n';
}

static void s_fixture_on_protocol_message(
    aws_event_stream_rpc_server_connection *connection,
    const aws_event_stream_rpc_message_args *message_args,
    void *user_data) {
    std::cerr << "😹 called s_fixture_on_protocol_message" << '\n';
    std::cerr << *message_args << '\n';

    switch(message_args->message_type) {
        case AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_CONNECT:
            doConnection(*connection);
            return;
        default:
            std::cerr << "Unhandled message type " << message_args->message_type << '\n';
            break;
    }
}

static void on_continuation(
    struct aws_event_stream_rpc_server_continuation_token *token,
    const struct aws_event_stream_rpc_message_args *message_args,
    void *user_data) {
    std::ignore = user_data;
    std::cerr << "🙀 called on_continutation" << std::endl;
    std::cerr << token << '\n';
    std::cerr << *message_args << '\n';

    using namespace ggapi;

    auto jsonBuf = Buffer::create();
    jsonBuf.insert(-1, message_args->payload->buffer, message_args->payload->len);
    auto json = jsonBuf.fromJson().unbox<Struct>();
    static const StringOrd publishToIoTCoreTopic{"aws.greengrass.PublishToIoTCore"};

    std::cerr << "[IPC] publishToIoTCoreTopic\n";
    std::ignore = Task::sendToTopic(publishToIoTCoreTopic, json);
    std::cerr << "[IPC] publish complete\n";
}

void on_continuation_close(
    struct aws_event_stream_rpc_server_continuation_token *token, void *user_data) {
    std::cerr << "⛔ called on_continuation_close" << std::endl;
    std::cerr << token << '\n';
}

static int on_incoming_stream(
    struct aws_event_stream_rpc_server_connection *connection,
    struct aws_event_stream_rpc_server_continuation_token *token,
    struct aws_byte_cursor operation_name,
    struct aws_event_stream_rpc_server_stream_continuation_options *continuation_options,
    void *user_data) {
    std::cerr << "😹 called on_server_incoming_stream" << std::endl;

    std::cerr << token << '\n';

    std::cerr.write(
        reinterpret_cast<const char *>(operation_name.ptr),
        static_cast<std::ptrdiff_t>(operation_name.len))
        << '\n';

    continuation_options->on_continuation = on_continuation;
    continuation_options->on_continuation_closed = on_continuation_close;

    return AWS_OP_SUCCESS;
}

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
        opts.SetSocketDomain(SocketDomain::Local);
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
    std::ignore = user_data;

    *connection_options = {
        .on_incoming_stream = on_incoming_stream,
        .on_connection_protocol_message = s_fixture_on_protocol_message,
        .user_data = nullptr,
    };

    std::cerr << "😾 s_on_new_server_connection called " << error_code << std::endl;
    return AWS_OP_SUCCESS;
}

static void s_fixture_on_server_connection_shutdown(
    struct aws_event_stream_rpc_server_connection *connection, int error_code, void *user_data) {
    std::ignore = connection;
    std::ignore = error_code;
    std::ignore = user_data;
    std::cerr << "😾 s_on_server_connection_shutdown called: " << error_code << std::endl;
}

static void s_on_listener_destroy(
    struct aws_event_stream_rpc_server_listener *server, void *user_data) {
    std::cerr << "😾 s_on_listener_destroy called." << std::endl;
}

bool IpcServer::onStart(ggapi::Struct data) {
    std::cerr << "onStart started 😹" << std::endl;
    static constexpr std::string_view socket_path = "/tmp/gglite-ipc.socket";

    if(std::filesystem::exists(socket_path)) {
        std::filesystem::remove(socket_path);
    }

    aws_event_stream_rpc_server_listener_options listenerOptions = {
        .host_name = socket_path.data(),
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
