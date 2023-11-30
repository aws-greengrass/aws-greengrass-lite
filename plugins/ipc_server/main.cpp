#include "aws/common/uuid.h"
#include "aws/event-stream/event_stream.h"
#include "aws/event-stream/event_stream_rpc.h"
#include "aws/http/request_response.h"
#include "util.hpp"
#include <algorithm>
#include <aws/crt/Allocator.h>
#include <aws/crt/Api.h>
#include <aws/crt/io/EventLoopGroup.h>
#include <aws/crt/io/SocketOptions.h>
#include <aws/event-stream/event_stream_rpc_server.h>
#include <aws/io/channel_bootstrap.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <future>
#include <ios>
#include <iostream>
#include <ostream>
#include <plugin.hpp>
#include <stdexcept>
#include <streambuf>
#include <system_error>
#include <tuple>
#include <type_traits>

using timestamp = std::chrono::duration<uint32_t, std::milli>;

using HeaderValue = std::variant<
    bool,
    uint8_t,
    uint16_t,
    uint32_t,
    uint64_t,
    util::Span<uint8_t, uint16_t>,
    std::string_view,
    timestamp,
    aws_uuid>;

template<typename To, size_t N>
// NOLINTNEXTLINE(*-c-arrays)
To from_network_bytes(const uint8_t (&buffer)[N]) noexcept {
    static_assert(N >= sizeof(To));
    static_assert(std::is_trivially_default_constructible_v<To>);
    static_assert(std::is_trivially_copy_assignable_v<To>);
    To to{};
    auto *punned = reinterpret_cast<uint8_t *>(&to);
    // network byte order
    std::reverse_copy(std::begin(buffer), std::next(std::begin(buffer), sizeof(To)), punned);
    return to;
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
            return from_network_bytes<uint16_t>(header.header_value.static_val);
        case AWS_EVENT_STREAM_HEADER_INT32:
            return from_network_bytes<uint32_t>(header.header_value.static_val);
        case AWS_EVENT_STREAM_HEADER_INT64:
            return from_network_bytes<uint64_t>(header.header_value.static_val);
        case AWS_EVENT_STREAM_HEADER_BYTE_BUF:
            // NOLINTNEXTLINE(*-reinterpret-cast)
            return util::Span{reinterpret_cast<uint8_t *>(variableData), header.header_value_len};
        case AWS_EVENT_STREAM_HEADER_STRING:
            return std::string_view{// NOLINTNEXTLINE(*-reinterpret-cast)
                                    reinterpret_cast<const char *>(variableData),
                                    header.header_value_len};
        case AWS_EVENT_STREAM_HEADER_TIMESTAMP:
            return timestamp{from_network_bytes<uint32_t>(header.header_value.static_val)};
        case AWS_EVENT_STREAM_HEADER_UUID: {
            return from_network_bytes<aws_uuid>(header.header_value.static_val);
        }
    }
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

template<typename IntT = uint64_t>
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

static void s_fixture_on_protocol_message(
    struct aws_event_stream_rpc_server_connection *connection,
    const struct aws_event_stream_rpc_message_args *message_args,
    void *user_data) {
    std::cerr << "😹 called s_fixture_on_protocol_message" << '\n';
    // print all headers and the payload
    using namespace std::string_view_literals;
    for(auto &&item : util::Span{message_args->headers, message_args->headers_count}) {
        auto &&[name, value] = parseHeader(item);
        if(name == ":message-type"sv
           && getIntOr(value, -1) == AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_CONNECT) {
            std::cerr << "Connect\n";
        }
        std::cerr << name << '=' << value << '\n';
    }
    std::string_view payload(
        reinterpret_cast<const char *>(message_args->payload->buffer), message_args->payload->len);
    std::cerr << payload << std::endl;
}
static int on_incoming_stream(
    struct aws_event_stream_rpc_server_connection *connection,
    struct aws_event_stream_rpc_server_continuation_token *token,
    struct aws_byte_cursor operation_name,
    struct aws_event_stream_rpc_server_stream_continuation_options *continuation_options,
    void *user_data) {
    std::cerr << "😹 called on_server_incoming_stream" << std::endl;

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
