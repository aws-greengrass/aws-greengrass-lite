#include "message.hpp"
#include "ipc_server.hpp"
#include <string_util.hpp>

namespace ipc_server {
    Aws::Crt::ByteBuf &Message::preparePayload() {
        return _payloadRef = preparePayload(_payloadBytes);
    }

    Aws::Crt::ByteBuf Message::preparePayload(Aws::Crt::Vector<uint8_t> &bytes) const {
        bytes.resize(0);
        ggapi::Buffer buffer;
        if(_payload.isBuffer()) {
            buffer = ggapi::Buffer(_payload);
        } else if(_payload.isScalar()) {
            auto str = _payload.unbox<std::string>();
            buffer = ggapi::Buffer::create();
            buffer.put(0, std::string_view{str});
        } else if(_payload) {
            buffer = _payload.toJson();
        }
        if(buffer) {
            if(buffer.size() > AWS_EVENT_STREAM_MAX_MESSAGE_SIZE) {
                throw ggapi::UnsupportedOperationError("Payload too large");
            }
            bytes.resize(buffer.size());
            buffer.get(0, bytes);
        }
        return Aws::Crt::ByteBufFromArray(bytes.data(), bytes.size());
    }

    const aws_event_stream_rpc_message_args &Message::prepare() {
        clearMessage();

        _copiedHeaders.reserve(_miscHeaders.size());
        for(const auto &header : _miscHeaders) {
            _copiedHeaders.push_back(header.getPair());
        }

        _message.headers = std::data(_copiedHeaders);
        _message.headers_count = _copiedHeaders.size();
        _message.payload = &preparePayload();
        _message.message_type = _messageType;
        _message.message_flags = _messageFlags;
        return _message;
    }

    const Header &Message::findHeader(std::string_view name, Header &def) const {
        for(const auto &h : _miscHeaders) {
            if(h.name() == name) {
                return h;
            }
        }
        return def;
    }

    Header &Message::findOrAddHeader(std::string_view name, const HeaderValue &v) {
        for(auto &h : _miscHeaders) {
            if(h.name() == name) {
                return h;
            }
        }
        return addHeader(Header(name, v));
    }

    Message Message::parse(const aws_event_stream_rpc_message_args &args) {
        Message message;
        auto headers = util::Span<aws_event_stream_header_value_pair, size_t>(
            args.headers, args.headers_count);
        for(const auto &h : headers) {
            message.addHeader(Header(h));
        }
        message.setType(args.message_type);
        message.setFlags(args.message_flags);
        auto buffer = ggapi::Buffer::create();
        auto data = util::Span<uint8_t, size_t>(args.payload->buffer, args.payload->len);
        buffer.put(0, data);
        Header nullHeader;
        auto h = message.findHeader(Header::CONTENT_TYPE_HEADER, nullHeader);
        bool isJson;
        if(h) {
            if(!h.isString()) {
                throw ggapi::ValidationError("Content-Type header is of wrong type");
            }
            std::string contentType = h.toString();
            if(contentType == ContentType::JSON) {
                isJson = true;
            } else if(contentType == ContentType::Text) {
                isJson = false;
            } else {
                throw ggapi::ValidationError("Content-Type is not recognized");
            }
        } else {
            // Default is JSON
            isJson = true;
        }
        if(isJson) {
            // Assume JSON formatted
            message.setPayload(buffer.fromJson());
        } else {
            // Assume Text
            message.setPayload(ggapi::Container::box(buffer.get<std::string>()));
        };
        return message;
    }

    Message Message::ofError(std::string_view message) {
        return ofError("aws#UnsupportedOperation", "aws#UnsupportedOperation", message);
    }

    Message Message::ofError(
        std::string_view model, std::string_view errorCode, std::string_view message) {

        // The exception kind is recognized and needs to be converted to
        // an IPC modelled error. This allows the IPC client model to
        // recognize the error and turn into a well-defined exception on
        // the client end.
        auto errorStruct = ggapi::Struct::create();
        errorStruct.put(keys._message, message);
        errorStruct.put(keys._errorCode, errorCode);
        errorStruct.put(keys._service, keys.greengrassIpcServiceName);
        Message m;
        m.setType(AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_APPLICATION_ERROR);
        m.setPayloadAndContentType(errorStruct);
        m.setServiceModelType(model);
        return m;
    }

    Message Message::ofError(const ggapi::GgApiError &err) {
        auto sym = err.kind();
        auto errorCode = _errorCodeMap.lookup(sym);
        if(errorCode.has_value()) {
            // Well defined exceptions that have a model mapping
            return ofError(errorCode.value(), errorCode.value(), err.what());
        }
        auto errorKind = err.kind().toString();
        if(util::startsWith(errorKind, ERROR_PREFIX)) {
            // Allow model pass through
            std::string suffix{util::trimStart(errorKind, ERROR_PREFIX)};
            return ofError(suffix, suffix, err.what());
        }
        // Unmodeled error
        return ofError(err.what());
    }

    Message &Message::setServiceModelType(std::string_view serviceModelType) {
        if(!serviceModelType.empty()) {
            if(serviceModelType.size() > std::numeric_limits<uint16_t>::max()) {
                throw ggapi::GgApiError("Service model type string is too large");
            }
            auto modelTypeText =
                util::Span<const char, uint16_t>(serviceModelType.data(), serviceModelType.size());
            Header &h = findOrAddHeader(Header::SERVICE_MODEL_TYPE_HEADER, false);
            h.setValue(modelTypeText);
        }
        return *this;
    }

    Message &Message::setPayloadAndContentType(
        const ggapi::Container &payload, std::string_view contentType) {
        auto contentTypeText =
            util::Span<const char, uint16_t>(contentType.data(), contentType.size());
        Header &h = findOrAddHeader(Header::CONTENT_TYPE_HEADER, false);
        h.setValue(contentTypeText);
        return setPayload(payload);
    }

    Message &Message::setPayloadAndContentType(const ggapi::Container &payload) {
        if(payload && payload.isScalar()) {
            // String or anything that can be literally converted to a string
            return setPayloadAndContentType(payload, ContentType::Text);
        } else {
            // Assumes Buffer is a preformatted JSON
            return setPayloadAndContentType(payload, ContentType::JSON);
        }
    }

    std::string Message::payloadAsString() const {
        Aws::Crt::Vector<uint8_t> bytes;
        auto ref = preparePayload(bytes);
        util::Span rv(ref.buffer, ref.len);
        std::string s;
        s.resize(rv.size());
        rv.copyTo(s.begin(), s.end());
        return s;
    }

    HeaderValue Header::value(const aws_event_stream_header_value_pair &other) {
        switch(other.header_value_type) {
            case AWS_EVENT_STREAM_HEADER_BOOL_FALSE:
                return false;
            case AWS_EVENT_STREAM_HEADER_BOOL_TRUE:
                return true;
            case AWS_EVENT_STREAM_HEADER_BYTE:
                return parseStatic<uint8_t>(other);
            case AWS_EVENT_STREAM_HEADER_INT16:
                return static_cast<int16_t>(aws_ntoh16(parseStatic<uint16_t>(other)));
            case AWS_EVENT_STREAM_HEADER_INT32:
                return static_cast<int32_t>(aws_ntoh32(parseStatic<uint32_t>(other)));
            case AWS_EVENT_STREAM_HEADER_INT64:
                return static_cast<int64_t>(aws_ntoh64(parseStatic<uint64_t>(other)));
            case AWS_EVENT_STREAM_HEADER_BYTE_BUF:
                return parseBuffer<uint8_t>(other);
            case AWS_EVENT_STREAM_HEADER_STRING:
                return parseBuffer<char>(other);
            case AWS_EVENT_STREAM_HEADER_TIMESTAMP:
                return header_value_types::timestamp{aws_ntoh64(parseStatic<uint64_t>(other))};
            case AWS_EVENT_STREAM_HEADER_UUID:
                return parseStatic<aws_uuid>(other);
            default:
                throw ggapi::ValidationError("Unknown header type");
        }
    }
} // namespace ipc_server
