#include "json_helper.h"
#include "data/environment.h"
#include <fstream>
#include <rapidjson/istreamwrapper.h>

namespace config {

    void TlogLine::serialize(
        data::Environment &environment, rapidjson::Writer<rapidjson::StringBuffer> &writer
    ) {
        writer.StartObject();

        writer.Key(TS);
        writer.Int64(timestamp.asMilliseconds());

        writer.Key(TP);
        writer.StartArray();
        for(const auto &i : topicPath) {
            writer.String(i.c_str());
        }
        writer.EndArray();

        const char *actionText;
        if((action & WhatHappened::interiorAdded) != WhatHappened::never) {
            actionText = "interiorAdded";
        } else if((action & WhatHappened::childChanged) != WhatHappened::never) {
            actionText = "childChanged";
        } else if((action & WhatHappened::childRemoved) != WhatHappened::never) {
            actionText = "childRemoved";
        } else if((action & WhatHappened::changed) != WhatHappened::never) {
            actionText = "changed";
        } else if((action & WhatHappened::removed) != WhatHappened::never) {
            actionText = "removed";
        } else if((action & WhatHappened::timestampUpdated) != WhatHappened::never) {
            actionText = "timestampUpdated";
        } else if((action & WhatHappened::initialized) != WhatHappened::never) {
            actionText = "initialized";
        } else {
            actionText = "";
        }
        writer.Key("W");
        writer.String(actionText);

        writer.Key("V");
        JsonHelper::serialize(environment, writer, value);

        writer.EndObject();
    }

    WhatHappened TlogLine::decodeWhatHappened(std::string_view whatHappenedString) {
        if(whatHappenedString == "changed") {
            return WhatHappened::changed;
        }
        if(whatHappenedString == "initialized") {
            return WhatHappened::initialized;
        }
        if(whatHappenedString == "childChanged") {
            return WhatHappened::childChanged;
        }
        if(whatHappenedString == "removed") {
            return WhatHappened::removed;
        }
        if(whatHappenedString == "childRemoved") {
            return WhatHappened::childRemoved;
        }
        if(whatHappenedString == "timestampUpdated") {
            return WhatHappened::timestampUpdated;
        }
        if(whatHappenedString == "interiorAdded") {
            return WhatHappened::interiorAdded;
        }
        return WhatHappened::never;
    }

    // NOLINTNEXTLINE(*-no-recursion)
    void JsonHelper::serialize(
        data::Environment &environment,
        rapidjson::Writer<rapidjson::StringBuffer> &writer,
        const data::StructElement &value
    ) {
        switch(value.getType()) {
        case data::ValueTypes::NONE:
            writer.Null();
            break;
        case data::ValueTypes::BOOL:
            writer.Bool(value.getBool());
            break;
        case data::ValueTypes::INT:
            writer.Int64(static_cast<int64_t>(value.getInt()));
            break;
        case data::ValueTypes::DOUBLE:
            writer.Double(value.getDouble());
            break;
        case data::ValueTypes::CONTAINER:
            if(value.isType<data::ListModelBase>()) {
                std::shared_ptr<data::ListModelBase> list =
                    value.castContainer<data::ListModelBase>()->copy();
                auto size = static_cast<int32_t>(list->size());
                writer.StartArray();
                for(int32_t idx = 0; idx < size; idx++) {
                    serialize(environment, writer, list->get(idx));
                }
                writer.EndArray();
            } else {
                std::shared_ptr<data::StructModelBase> s =
                    value.castContainer<data::StructModelBase>()->copy();
                std::vector<data::StringOrd> keys = s->getKeys();
                writer.StartObject();
                for(const auto &i : keys) {
                    std::string k = environment.stringTable.getString(i);
                    writer.Key(k.c_str());
                    serialize(environment, writer, s->get(i));
                }
                writer.EndObject();
            }
            break;
        default:
            writer.String(value.getString().c_str());
            break;
        }
    }

    TlogLine TlogLine::readRecord(data::Environment &environment, std::ifstream &stream) {
        TlogLine tlogLine;
        tlogLine.deserialize(environment, stream);
        return tlogLine;
    }

    bool TlogLine::deserialize(data::Environment &environment, std::ifstream &stream) {
        JsonReader reader(environment);
        reader.push(std::make_unique<TlogLineResponder>(reader, *this, false));
        rapidjson::ParseResult result = reader.read(stream);
        if(result) {
            return true;
        }
        if(result.Code() == rapidjson::ParseErrorCode::kParseErrorDocumentEmpty) {
            return false; // failure occured prior to parsing
        }
        throw std::runtime_error("JSON structure invalid");
    }

    rapidjson::ParseResult JsonReader::read(std::ifstream &stream) {
        if(!stream.is_open()) {
            throw std::invalid_argument("JSON stream is not open");
        }
        rapidjson::IStreamWrapper wrapper{stream};
        rapidjson::Reader reader;
        return reader.Parse<
            rapidjson::kParseStopWhenDoneFlag | rapidjson::kParseCommentsFlag
            | rapidjson::kParseFullPrecisionFlag | rapidjson::kParseNanAndInfFlag>(wrapper, *this);
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
            _state = JsonState::ExpectKey;
            std::shared_ptr<data::SharedStruct> target(
                std::make_shared<data::SharedStruct>(_reader.environment())
            );
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
            _state = JsonState::ExpectKey;
            std::shared_ptr<data::SharedList> target(
                std::make_shared<data::SharedList>(_reader.environment())
            );
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
                std::make_shared<data::SharedStruct>(_reader.environment())
            );
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
                std::make_shared<data::SharedList>(_reader.environment())
            );
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
        const std::string &key, data::StructElement value
    ) {
        _target->put(key, value);
        return true;
    }

    data::StructElement JsonSharedStructResponder::buildValue() {
        return data::StructElement(std::static_pointer_cast<data::ContainerModelBase>(_target));
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
        return data::StructElement(std::static_pointer_cast<data::ContainerModelBase>(_target));
    }

    bool TlogLineResponder::parseKeyValue(const std::string &key, data::StructElement value) {
        if(key == TlogLine::TS) {
            _tlogLine.timestamp = Timestamp(static_cast<int64_t>(value.getInt()));
            return true;
        } else if(key == TlogLine::W) {
            _tlogLine.action = TlogLine::decodeWhatHappened(value.getString());
            return true;
        } else if(key == TlogLine::V) {
            _tlogLine.value = value;
            return true;
        } else {
            // Note, TlogLine::TP is handled in parseStartArray
            // Ignore other values
            return true;
        }
    }

    bool TlogLineResponder::parseStartArray() {
        if(_state != JsonState::ExpectValue) {
            return JsonStructResponder::parseStartArray();
        }
        if(_key == TlogLine::TP) {
            _reader.push(std::make_unique<TlogLinePathResponder>(_reader, _tlogLine, true));
            _state = JsonState::ExpectKey; // once done
            return true;
        }
        if(_key == TlogLine::V) {
            return JsonStructResponder::parseStartArray();
        }
        return false;
    }

    bool TlogLineResponder::parseStartObject() {
        if(_state == JsonState::ExpectStartObject
           || _state == JsonState::ExpectValue && _key == TlogLine::V) {
            return JsonStructResponder::parseStartObject();
        } else {
            return false;
        }
    }

    bool TlogLinePathResponder::parseStartArray() {
        if(_state == JsonState::ExpectStartArray) {
            _state = JsonState::ExpectValue;
            return true;
        } else {
            return false;
        }
    }

    bool TlogLinePathResponder::parseEndArray() {
        if(_state == JsonState::ExpectValue) {
            _tlogLine.topicPath = _path;
            return _reader.pop({});
        } else {
            return false;
        }
    }

    bool TlogLinePathResponder::parseValue(data::StructElement value) {
        if(_state == JsonState::ExpectValue) {
            _path.emplace_back(value.getString());
            return true;
        } else {
            return false;
        }
    }

    bool TlogLinePathResponder::parseStartObject() {
        return false;
    }

} // namespace config
