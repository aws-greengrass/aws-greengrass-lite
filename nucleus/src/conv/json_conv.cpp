#include "json_conv.hpp"
#include "data/shared_buffer.hpp"
#include "data/struct_model.hpp"
#include "scope/context_full.hpp"
#include <fstream>
#include <rapidjson/istreamwrapper.h>

namespace conv {

    std::shared_ptr<data::SharedBuffer> JsonHelper::serializeToBuffer(
        const std::shared_ptr<scope::Context> &context,
        const std::shared_ptr<data::TrackedObject> &obj) {

        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        serialize(context, writer, obj);
        auto targetBuffer = std::make_shared<data::SharedBuffer>(context);
        targetBuffer->put(0, data::ConstMemoryView(buffer.GetString(), buffer.GetSize()));
        return targetBuffer;
    }

    // NOLINTNEXTLINE(*-no-recursion)
    void JsonHelper::serialize(
        const std::shared_ptr<scope::Context> &context,
        rapidjson::Writer<rapidjson::StringBuffer> &writer,
        const data::StructElement &value) {
        std::visit(
            [&context, &writer](auto &&x) { serialize(context, writer, x); }, value.get().base());
    }

    void JsonHelper::serialize(
        const std::shared_ptr<scope::Context> &,
        rapidjson::Writer<rapidjson::StringBuffer> &writer,
        std::monostate) {
        writer.Null();
    }

    void JsonHelper::serialize(
        const std::shared_ptr<scope::Context> &,
        rapidjson::Writer<rapidjson::StringBuffer> &writer,
        bool b) {
        writer.Bool(b);
    }

    void JsonHelper::serialize(
        const std::shared_ptr<scope::Context> &,
        rapidjson::Writer<rapidjson::StringBuffer> &writer,
        uint64_t i) {
        writer.Uint64(i);
    }

    void JsonHelper::serialize(
        const std::shared_ptr<scope::Context> &,
        rapidjson::Writer<rapidjson::StringBuffer> &writer,
        double d) {
        writer.Double(d);
    }

    void JsonHelper::serialize(
        const std::shared_ptr<scope::Context> &,
        rapidjson::Writer<rapidjson::StringBuffer> &writer,
        const std::string &str) {
        writer.String(str.c_str());
    }

    void JsonHelper::serialize(
        const std::shared_ptr<scope::Context> &,
        rapidjson::Writer<rapidjson::StringBuffer> &writer,
        const data::Symbol &sym) {
        writer.String(sym.toString().c_str());
    }

    void JsonHelper::serialize(
        const std::shared_ptr<scope::Context> &context,
        rapidjson::Writer<rapidjson::StringBuffer> &writer,
        const std::shared_ptr<data::TrackedObject> &obj) {
        std::shared_ptr<data::ListModelBase> asList =
            std::dynamic_pointer_cast<data::ListModelBase>(obj);
        if(asList) {
            asList = asList->copy();
            auto size = static_cast<int32_t>(asList->size());
            writer.StartArray();
            for(int32_t idx = 0; idx < size; idx++) {
                serialize(context, writer, asList->get(idx));
            }
            writer.EndArray();
            return;
        }

        std::shared_ptr<data::StructModelBase> asStruct =
            std::dynamic_pointer_cast<data::StructModelBase>(obj);
        if(asStruct) {
            asStruct = asStruct->copy();
            std::vector<data::Symbol> keys = asStruct->getKeys();
            writer.StartObject();
            for(const auto &i : keys) {
                std::string k = i.toString();
                writer.Key(k.c_str());
                serialize(context, writer, asStruct->get(i));
            }
            writer.EndObject();
            return;
        }

        // Other objects are ignored
    }

    rapidjson::ParseResult JsonReader::readStream(std::istream &stream) {
        rapidjson::IStreamWrapper wrapper{stream};
        rapidjson::Reader reader;
        return reader.Parse<
            rapidjson::kParseStopWhenDoneFlag | rapidjson::kParseCommentsFlag
            | rapidjson::kParseFullPrecisionFlag | rapidjson::kParseNanAndInfFlag>(wrapper, *this);
    }

    rapidjson::ParseResult JsonReader::read(std::ifstream &stream) {
        if(!stream.is_open()) {
            throw std::invalid_argument("JSON stream is not open");
        }
        return readStream(stream);
    }

    bool JsonStructResponder::parseValue(data::StructElement value) {
        if(_state == JsonState::ExpectValue) {
            _state = JsonState::ExpectKey;
            return parseKeyValue(_key, std::move(value));
        } else {
            return false;
        }
    }

    bool JsonStructResponder::parseKey(const std::string_view &key) {
        if(_state == JsonState::ExpectKey) {
            _state = JsonState::ExpectValue;
            _key = key;
            return true;
        } else {
            return false;
        }
    }

    bool JsonStructResponder::parseStartObject() {
        if(_state == JsonState::ExpectStartObject) {
            _state = JsonState::ExpectKey;
            return true;
        } else if(_state == JsonState::ExpectValue) {
            std::shared_ptr<data::SharedStruct> target(
                std::make_shared<data::SharedStruct>(_reader.refContext()));
            _reader.push(std::make_unique<JsonSharedStructResponder>(_reader, target, true));
            return true;
        } else {
            return false;
        }
    }

    bool JsonStructResponder::parseEndObject() {
        if(_state == JsonState::ExpectKey) {
            // 'pop' will delete this structure
            return _reader.pop(buildValue());
        } else {
            return false;
        }
    }

    bool JsonStructResponder::parseStartArray() {
        if(_state == JsonState::ExpectValue) {
            std::shared_ptr<data::SharedList> target(
                std::make_shared<data::SharedList>(_reader.refContext()));
            _reader.push(std::make_unique<JsonSharedListResponder>(_reader, target, true));
            return true;
        } else {
            return false;
        }
    }

    bool JsonStructResponder::parseEndArray() {
        return false;
    }

    bool JsonArrayResponder::parseKey(const std::string_view &key) {
        return false;
    }

    bool JsonArrayResponder::parseStartObject() {
        if(_state == JsonState::ExpectValue) {
            std::shared_ptr<data::SharedStruct> target(
                std::make_shared<data::SharedStruct>(_reader.refContext()));
            _reader.push(std::make_unique<JsonSharedStructResponder>(_reader, target, true));
            return true;
        } else {
            return false;
        }
    }

    bool JsonArrayResponder::parseEndObject() {
        return false;
    }

    bool JsonArrayResponder::parseStartArray() {
        if(_state == JsonState::ExpectStartArray) {
            _state = JsonState::ExpectValue;
            return true;
        } else if(_state == JsonState::ExpectValue) {
            std::shared_ptr<data::SharedList> target(
                std::make_shared<data::SharedList>(_reader.refContext()));
            _reader.push(std::make_unique<JsonSharedListResponder>(_reader, target, true));
            return true;
        } else {
            return false;
        }
    }

    bool JsonArrayResponder::parseEndArray() {
        if(_state == JsonState::ExpectValue) {
            // 'pop' will delete this object
            return _reader.pop(buildValue());
        } else {
            return false;
        }
    }

    bool JsonSharedStructResponder::parseKeyValue(
        const std::string &key, data::StructElement value) {
        _target->put(key, value);
        return true;
    }

    data::StructElement JsonSharedStructResponder::buildValue() {
        return {std::static_pointer_cast<data::ContainerModelBase>(_target)};
    }

    bool JsonSharedListResponder::parseValue(data::StructElement value) {
        if(_state == JsonState::ExpectValue) {
            _target->put(_idx++, value);
            return true;
        } else {
            return false;
        }
    }

    data::StructElement JsonSharedListResponder::buildValue() {
        return {std::static_pointer_cast<data::ContainerModelBase>(_target)};
    }

    bool JsonElementResponder::parseValue(data::StructElement value) {
        _value = value;
        return _reader.pop(value);
    }
    bool JsonElementResponder::parseStartObject() {
        auto target = std::make_shared<data::SharedStruct>(_reader.refContext());
        _reader.push(std::make_unique<JsonSharedStructResponder>(_reader, target, true));
        return true;
    }
    bool JsonElementResponder::parseStartArray() {
        auto target = std::make_shared<data::SharedList>(_reader.refContext());
        _reader.push(std::make_unique<JsonSharedListResponder>(_reader, target, true));
        return true;
    }
    bool JsonElementResponder::parseKey(const std::string_view &view) {
        return false;
    }
    bool JsonElementResponder::parseEndObject() {
        return false;
    }
    bool JsonElementResponder::parseEndArray() {
        return false;
    }
} // namespace conv
