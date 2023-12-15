#pragma once

#include <util.hpp>

#include <aws/common/byte_order.h>
#include <aws/common/uuid.h>
#include <aws/event-stream/event_stream.h>

#include <chrono>
#include <ios>
#include <iostream>
#include <string_view>
#include <type_traits>
#include <variant>

#include <cstdint>
#include <cstring>

namespace Headervaluetypes {
    using timestamp = std::chrono::duration<uint64_t, std::milli>;
    using bytebuffer = util::Span<uint8_t>;
    using stringbuffer = util::Span<char>;
} // namespace Headervaluetypes

using HeaderValue = std::variant<
    bool,
    uint8_t,
    int16_t,
    int32_t,
    int64_t,
    Headervaluetypes::bytebuffer,
    Headervaluetypes::stringbuffer,
    Headervaluetypes::timestamp,
    aws_uuid>;

template<typename T>
constexpr bool is_variable_length_value = std::is_same_v<T, Headervaluetypes::bytebuffer>
                                          || std::is_same_v<T, Headervaluetypes::stringbuffer>;

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
        // NOLINTNEXTLINE(*-reinterpret-cast)
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
        // convert to network byte order
        // NOLINTNEXTLINE(*-reinterpret-cast)
        auto *punned = reinterpret_cast<const uint8_t *>(&from);
        std::reverse_copy(punned, std::next(punned, sizeof(From)), std::begin(buffer));
    }
}

// NOLINTBEGIN(*-union-access)
static HeaderValue getValue(const aws_event_stream_header_value_pair &header) noexcept {
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
            return Headervaluetypes::bytebuffer{variableData, header.header_value_len};
        case AWS_EVENT_STREAM_HEADER_STRING:
            return Headervaluetypes::stringbuffer{// NOLINTNEXTLINE(*-reinterpret-cast)
                                                  reinterpret_cast<char *>(variableData),
                                                  header.header_value_len};
        case AWS_EVENT_STREAM_HEADER_TIMESTAMP:
            return Headervaluetypes::timestamp{
                from_network_bytes<uint64_t>(header.header_value.static_val)};
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
            } else if constexpr(std::is_same_v<Headervaluetypes::bytebuffer, T>) {
                return AWS_EVENT_STREAM_HEADER_BYTE_BUF;
            } else if constexpr(std::is_same_v<Headervaluetypes::stringbuffer, T>) {
                return AWS_EVENT_STREAM_HEADER_STRING;
            } else if constexpr(std::is_same_v<Headervaluetypes::timestamp, T>) {
                return AWS_EVENT_STREAM_HEADER_TIMESTAMP;
            } else if constexpr(std::is_same_v<aws_uuid, T>) {
                return AWS_EVENT_STREAM_HEADER_UUID;
            }
        },
        variant);
}

template<typename T>
static std::enable_if_t<std::is_constructible_v<HeaderValue, T>, aws_event_stream_header_value_pair>
makeHeader(std::string_view name, const T &val) noexcept {
    aws_event_stream_header_value_pair args{};

    args.header_name_len = name.copy(std::data(args.header_name), std::size(args.header_name));

    if constexpr(is_variable_length_value<T>) {
        // NOLINTNEXTLINE(*-reinterpret-cast)
        args.header_value.variable_len_val = reinterpret_cast<uint8_t *>(std::data(val));
        args.header_value_len = std::size(val);
    } else { // static-sized types
        to_network_bytes(args.header_value.static_val, val);
        args.header_value_len = sizeof(val);
    }

    args.header_value_type = getType(val);

    return args;
}

// NOLINTEND(*-union-access)

inline auto parseHeader(const aws_event_stream_header_value_pair &pair) {
    return std::make_pair(
        std::string_view{std::data(pair.header_name), pair.header_name_len}, getValue(pair));
}

//
// Operator Overloads
//

inline std::ostream &operator<<(std::ostream &os, HeaderValue v) {
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
            } else if constexpr(std::is_same_v<Headervaluetypes::timestamp, T>) {
                os << val.count() << "ms";
            } else if constexpr(
                std::is_same_v<Headervaluetypes::stringbuffer, T>
                || std::is_same_v<Headervaluetypes::bytebuffer, T>) {
                // NOLINTNEXTLINE(*-reinterpret-cast)
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

namespace Headers {
    using namespace std::string_view_literals;
    inline constexpr auto ContentType = ":content-type"sv;
    inline constexpr auto ServiceModelType = "service-model-type"sv;
}; // namespace Headers
