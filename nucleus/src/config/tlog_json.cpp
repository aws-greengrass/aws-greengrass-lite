#include "tlog_json.hpp"
#include "scope/context_full.hpp"
#include <fstream>

namespace config {

    void TlogLine::serialize(
        const std::shared_ptr<scope::Context> &context,
        rapidjson::Writer<rapidjson::StringBuffer> &writer) {
        writer.StartObject();

        writer.Key(TS);
        writer.Uint64(timestamp.asMilliseconds());

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
        conv::JsonHelper::serialize(context, writer, value);

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

    TlogLine TlogLine::readRecord(
        const std::shared_ptr<scope::Context> &context, std::ifstream &stream) {
        TlogLine tlogLine;
        tlogLine.deserialize(context, stream);
        return tlogLine;
    }

    bool TlogLine::deserialize(
        const std::shared_ptr<scope::Context> &context, std::ifstream &stream) {
        conv::JsonReader reader(context);
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
        if(_state != conv::JsonState::ExpectValue) {
            return JsonStructResponder::parseStartArray();
        }
        if(_key == TlogLine::TP) {
            _reader.push(std::make_unique<TlogLinePathResponder>(_reader, _tlogLine, true));
            _state = conv::JsonState::ExpectKey; // once done
            return true;
        }
        if(_key == TlogLine::V) {
            return JsonStructResponder::parseStartArray();
        }
        return false;
    }

    bool TlogLineResponder::parseStartObject() {
        if(_state == conv::JsonState::ExpectStartObject
           || _state == conv::JsonState::ExpectValue && _key == TlogLine::V) {
            return JsonStructResponder::parseStartObject();
        } else {
            return false;
        }
    }

    bool TlogLinePathResponder::parseStartArray() {
        if(_state == conv::JsonState::ExpectStartArray) {
            _state = conv::JsonState::ExpectValue;
            return true;
        } else {
            return false;
        }
    }

    bool TlogLinePathResponder::parseEndArray() {
        if(_state == conv::JsonState::ExpectValue) {
            _tlogLine.topicPath = _path;
            return _reader.pop({});
        } else {
            return false;
        }
    }

    bool TlogLinePathResponder::parseValue(data::StructElement value) {
        if(_state == conv::JsonState::ExpectValue) {
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
