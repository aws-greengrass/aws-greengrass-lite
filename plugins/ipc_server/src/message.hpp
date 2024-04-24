#pragma once
#include <api_standard_errors.hpp>
#include <containers.hpp>
#include <lookup_table.hpp>
#include <plugin.hpp>
#include <span.hpp>

#include <chrono>
#include <ios>
#include <iostream>
#include <optional>
#include <shared_device_sdk.hpp>
#include <string_view>
#include <type_traits>
#include <variant>

#include <cstdint>
#include <cstring>

namespace ipc_server {
    using namespace std::string_view_literals;

    namespace header_value_types {
        using timestamp = std::chrono::duration<uint64_t, std::milli>;
        using bytebuffer = util::Span<const uint8_t, uint16_t>;
        using stringbuffer = util::Span<const char, uint16_t>;
    } // namespace header_value_types

    /**
     * Note that the variable-length values are by reference
     */
    using HeaderValue = std::variant<
        bool,
        uint8_t,
        int16_t,
        int32_t,
        int64_t,
        header_value_types::bytebuffer,
        header_value_types::stringbuffer,
        header_value_types::timestamp,
        aws_uuid>;

    using MessageType = aws_event_stream_rpc_message_type;

    namespace ContentType {
        inline constexpr auto JSON = "application/json"sv;
        inline constexpr auto Text = "text/plain"sv;
    } // namespace ContentType

    class Header {
    public:
        inline static constexpr auto VERSION_HEADER = ":version"sv;
        inline static constexpr auto CONTENT_TYPE_HEADER = ":content-type"sv;
        inline static constexpr auto SERVICE_MODEL_TYPE_HEADER = "service-model-type"sv;

    private:
        std::vector<uint8_t> _extraStorage;
        aws_event_stream_header_value_pair _pair = {};

        template<typename T, int sz = sizeof(T)>
        [[nodiscard]] T *writableStatic() {
            // NOLINTNEXTLINE(*-type-union-access)
            static_assert(sz <= sizeof(_pair.header_value.static_val));
            _pair.header_value_len = sz;
            // NOLINTNEXTLINE(*-type-union-access,*-type-reinterpret-cast)
            return reinterpret_cast<T *>(_pair.header_value.static_val);
        }

        template<typename T, int sz = sizeof(T)>
        [[nodiscard]] static const T &parseStatic(const aws_event_stream_header_value_pair &other) {
            // NOLINTNEXTLINE(*-type-union-access)
            static_assert(sz <= sizeof(other.header_value.static_val));
            if(other.header_value_len != sz) {
                throw ggapi::ValidationError("Invalid Header");
            }
            // NOLINTNEXTLINE(*-type-union-access,*-type-reinterpret-cast)
            return *reinterpret_cast<const T *>(other.header_value.static_val);
        }

        template<typename T>
        [[nodiscard]] static util::Span<const T, uint16_t> parseBuffer(
            const aws_event_stream_header_value_pair &other) {
            // NOLINTNEXTLINE(*-type-union-access,*-type-reinterpret-cast)
            auto p = reinterpret_cast<const T *>(other.header_value.variable_len_val);
            return {p, other.header_value_len};
        }

        [[nodiscard]] static std::string_view name(
            const aws_event_stream_header_value_pair &other) noexcept {
            return {static_cast<const char *>(other.header_name), other.header_name_len};
        }

        [[nodiscard]] static HeaderValue value(const aws_event_stream_header_value_pair &other);

        void init(bool b) {
            _pair.header_value_type =
                b ? AWS_EVENT_STREAM_HEADER_BOOL_TRUE : AWS_EVENT_STREAM_HEADER_BOOL_FALSE;
            _pair.header_value_len = 0;
        }

        void init(uint8_t v) {
            _pair.header_value_type = AWS_EVENT_STREAM_HEADER_BYTE;
            *writableStatic<uint8_t>() = v;
        }

        void init(int16_t v) {
            _pair.header_value_type = AWS_EVENT_STREAM_HEADER_INT16;
            *writableStatic<uint16_t>() = aws_hton16(v);
        }

        void init(int32_t v) {
            _pair.header_value_type = AWS_EVENT_STREAM_HEADER_INT32;
            *writableStatic<uint32_t>() = aws_hton32(v);
        }

        void init(int64_t v) {
            _pair.header_value_type = AWS_EVENT_STREAM_HEADER_INT64;
            *writableStatic<uint64_t>() = aws_hton64(v);
        }

        void init(const header_value_types::timestamp &ts) {
            _pair.header_value_type = AWS_EVENT_STREAM_HEADER_TIMESTAMP;
            // Timestamp is represented as 64-bit
            *writableStatic<uint64_t>() = aws_hton64(ts.count());
        }

        void init(const aws_uuid &uuid) {
            _pair.header_value_type = AWS_EVENT_STREAM_HEADER_UUID;
            // NOTE, UUID's are already network byte ordered
            *writableStatic<aws_uuid>() = uuid;
        }

        void init(header_value_types::bytebuffer buf) {
            _pair.header_value_type = AWS_EVENT_STREAM_HEADER_BYTE_BUF;
            _extraStorage.resize(buf.size());
            buf.copyTo(_extraStorage.begin(), _extraStorage.end());
            // Next line assumes _extraStorage is never reallocated/repositioned in memory
            // NOLINTNEXTLINE(*-type-union-access)
            _pair.header_value.variable_len_val = _extraStorage.data();
            _pair.header_value_len = buf.size();
        }

        void init(header_value_types::stringbuffer buf) {
            _pair.header_value_type = AWS_EVENT_STREAM_HEADER_STRING;
            _extraStorage.resize(buf.size());
            buf.copyTo(_extraStorage.begin(), _extraStorage.end());
            // Next line assumes _extraStorage is never reallocated/repositioned in memory
            // NOLINTNEXTLINE(*-type-union-access)
            _pair.header_value.variable_len_val = _extraStorage.data();
            _pair.header_value_len = buf.size();
        }

        static void writeImpl(std::ostream &strm, bool b) {
            strm.flags(std::ios::boolalpha);
            strm << b;
        }

        static void writeImpl(std::ostream &strm, uint8_t v) {
            strm << v;
        }

        static void writeImpl(std::ostream &strm, int16_t v) {
            strm << v;
        }

        static void writeImpl(std::ostream &strm, int32_t v) {
            strm << v;
        }

        static void writeImpl(std::ostream &strm, int64_t v) {
            strm << v;
        }

        static void writeImpl(std::ostream &strm, header_value_types::timestamp ts) {
            strm << ts.count() << "ms";
        }

        static void writeImpl(std::ostream &strm, const header_value_types::stringbuffer &v) {
            auto bytes = util::as_bytes(v);
            strm.write(bytes.data(), bytes.size());
        }

        static void writeImpl(std::ostream &strm, const header_value_types::bytebuffer &v) {
            auto bytes = util::as_bytes(v);
            strm.write(bytes.data(), bytes.size());
        }

        static void writeImpl(std::ostream &strm, const aws_uuid &uuid) {
            strm.flags(std::ios::hex | std::ios::uppercase);
            for(auto &&v : uuid.uuid_data) {
                strm << v;
            }
        }

    public:
        Header() noexcept {
            _pair.value_owned = 0;
        }

        Header(const Header &other) : Header() {
            setName(other.name());
            setValue(other.value());
        }

        Header &operator=(const Header &other) {
            setName(other.name());
            setValue(other.value());
            return *this;
        }

        Header(Header &&other) noexcept = default;
        Header &operator=(Header &&other) noexcept = default;
        ~Header() noexcept = default;

        explicit Header(std::string_view name) : Header() {
            setName(name);
        }

        explicit Header(std::string_view name, const HeaderValue &value) : Header(name) {
            setValue(value);
        }

        explicit Header(const aws_event_stream_header_value_pair &other) : Header() {
            setName(name(other));
            setValue(value(other));
        }

        Header &setName(std::string_view name) {
            _pair.header_name_len =
                name.copy(std::data(_pair.header_name), std::size(_pair.header_name));
            return *this;
        }

        Header &setValue(const HeaderValue &value) {
            std::visit([this](const auto &value) -> void { this->init(value); }, value);
            return *this;
        }

        [[nodiscard]] std::string_view name() const noexcept {
            return name(_pair);
        }

        [[nodiscard]] HeaderValue value() const {
            return value(_pair);
        }

        [[nodiscard]] const aws_event_stream_header_value_pair &getPair() const noexcept {
            return _pair;
        }

        static void write(std::ostream &strm, const HeaderValue &value) noexcept {
            try {
                auto flags = strm.flags();
                std::visit([&strm](const auto &v) -> void { writeImpl(strm, v); }, value);
                strm.flags(flags);
            } catch(...) {
                strm << "(error parsing)";
            }
        }

        explicit operator bool() const noexcept {
            return _pair.header_value_len != 0;
        }

        bool operator!() const noexcept {
            return _pair.header_value_len == 0;
        }

        [[nodiscard]] bool isString() const noexcept {
            return _pair.header_value_type == AWS_EVENT_STREAM_HEADER_STRING;
        }

        [[nodiscard]] bool isData() const noexcept {
            return _pair.header_value_type == AWS_EVENT_STREAM_HEADER_BYTE_BUF;
        }

        [[nodiscard]] std::string toString() const noexcept {
            std::stringstream strm;
            write(strm, value());
            return strm.str();
        }
    };

    /**
     * Encapsulates a message so that other code can form messages as a whole
     */
    class Message {
        static constexpr auto ERROR_PREFIX = "IPC::Modeled::"sv;

        std::vector<Header> _miscHeaders;
        std::vector<aws_event_stream_header_value_pair> _copiedHeaders;
        Aws::Crt::Vector<uint8_t> _payloadBytes;
        ggapi::Container _payload;
        MessageType _messageType{AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_APPLICATION_MESSAGE};
        uint32_t _messageFlags{0};
        aws_event_stream_rpc_message_args _message{};
        Aws::Crt::ByteBuf _payloadRef{};

        [[nodiscard]] Aws::Crt::ByteBuf preparePayload(Aws::Crt::Vector<uint8_t> &) const;

        /**
         * GG-Interop: This table must be kept up to date with modelled errors
         */
        static inline const util::LookupTable _errorCodeMap{
            ggapi::AccessDeniedError::KIND,
            "aws#AccessDenied"sv,
            ggapi::InternalServerException::KIND,
            "aws#InternalServerException"sv,
            ggapi::ValidationError::KIND,
            "aws#ValidationError"sv,
            ggapi::UnsupportedOperationError::KIND,
            "aws#UnsupportedOperation"sv};

    public:
        Message() noexcept {
            clearMessage();
        }
        Message(const Message &other) = default;
        Message(Message &&other) noexcept = default;
        Message &operator=(const Message &other) = default;
        Message &operator=(Message &&other) noexcept = default;
        ~Message() noexcept = default;

        void clearMessage() noexcept {
            _message = {};
            _payloadRef = {};
            _payloadBytes.resize(0);
            _copiedHeaders.resize(0);
        }

        Header &addHeader(Header header) {
            return _miscHeaders.emplace_back(std::move(header));
        }

        Message &setPayload(const ggapi::Container &payload) {
            _payload = payload;
            return *this;
        }

        [[nodiscard]] ggapi::Container getPayload() const {
            return _payload;
        }

        Message &setPayloadAndContentType(const ggapi::Container &payload);
        Message &setPayloadAndContentType(
            const ggapi::Container &payload, std::string_view contentType);

        [[nodiscard]] MessageType getType() const {
            return _messageType;
        }

        Message &setServiceModelType(std::string_view serviceModelType);

        Message &setType(MessageType type) {
            _messageType = type;
            return *this;
        }

        /**
         * Create/re-create payload buffer from the provided content.
         * @return Payload data
         */
        Aws::Crt::ByteBuf &preparePayload();

        /**
         * Create/re-create the rpc message args from provided content, header and flags.
         * @return Args for use in sending a message
         */
        const aws_event_stream_rpc_message_args &prepare();

        /**
         * Parse an aws message args into a message structure for working.
         * @return aws message structure.
         */
        static Message parse(const aws_event_stream_rpc_message_args &);

        /**
         * Translate a GGAPI error into a correctly formed error message that understands
         * the Greengrass model.
         *
         * @param service Service causing error
         * @param err Error to translate
         * @return error message
         */
        static Message ofError(const ggapi::GgApiError &err);

        /**
         * Generic unmodeled error
         * @param error Error text string
         * @param message Error message string
         * @return error message
         */
        static Message ofError(std::string_view message);

        static Message ofError(
            std::string_view model, std::string_view errorCode, std::string_view message);

        Message &setConnectionAccepted(bool accepted = true) {
            if(accepted) {
                _messageFlags |= AWS_EVENT_STREAM_RPC_MESSAGE_FLAG_CONNECTION_ACCEPTED;
            } else {
                _messageFlags &= ~AWS_EVENT_STREAM_RPC_MESSAGE_FLAG_CONNECTION_ACCEPTED;
            }
            return *this;
        }

        Message &setTerminateStream(bool accepted = true) {
            if(accepted) {
                _messageFlags |= AWS_EVENT_STREAM_RPC_MESSAGE_FLAG_TERMINATE_STREAM;
            } else {
                _messageFlags &= ~AWS_EVENT_STREAM_RPC_MESSAGE_FLAG_TERMINATE_STREAM;
            }
            return *this;
        }

        [[nodiscard]] uint32_t getFlags() const {
            return _messageFlags;
        }

        Message &setFlags(uint32_t flags) {
            _messageFlags = flags;
            return *this;
        }

        [[nodiscard]] bool isConnectionAccepted() const {
            return (_messageFlags & AWS_EVENT_STREAM_RPC_MESSAGE_FLAG_CONNECTION_ACCEPTED) != 0;
        }

        [[nodiscard]] bool isTerminateStream() const {
            return (_messageFlags & AWS_EVENT_STREAM_RPC_MESSAGE_FLAG_TERMINATE_STREAM) != 0;
        }

        [[nodiscard]] const std::vector<Header> &headers() const {
            return _miscHeaders;
        }

        [[nodiscard]] const Header &findHeader(std::string_view name, Header &def) const;

        Header &findOrAddHeader(std::string_view name, const HeaderValue &v);

        [[nodiscard]] std::string payloadAsString() const;
    };

    //
    // Operator overloads for debugging
    //

    inline std::ostream &operator<<(std::ostream &os, const Header &header) {
        os << header.name();
        os << '=';
        Header::write(os, header.value());
        return os;
    }

    inline std::ostream &operator<<(std::ostream &os, const Message &message) {
        for(auto &&item : message.headers()) {
            os << item << ';';
        }
        os << "{flags=" << message.getFlags() << "};";
        os << message.payloadAsString();
        return os;
    }

} // namespace ipc_server
