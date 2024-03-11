#pragma once

#include "config/config_manager.hpp"
#include "conv/json_conv.hpp"
#include "scope/context.hpp"

namespace config {

    struct TlogLine;

    class TlogLineResponder : public conv::JsonStructResponder {
        TlogLine &_tlogLine;

    public:
        explicit TlogLineResponder(conv::JsonReader &reader, TlogLine &line, bool started)
            : JsonStructResponder(reader, started), _tlogLine(line) {
        }

        bool parseKeyValue(const std::string &key, data::StructElement value) override;
        bool parseStartArray() override;
        bool parseStartObject() override;
    };

    class TlogLinePathResponder : public conv::JsonArrayResponder {
        TlogLine &_tlogLine;
        std::vector<std::string> _path;

    public:
        explicit TlogLinePathResponder(conv::JsonReader &reader, TlogLine &line, bool started)
            : JsonArrayResponder(reader, started), _tlogLine(line) {
        }

        bool parseValue(data::StructElement value) override;
        bool parseStartArray() override;
        bool parseStartObject() override;
        bool parseEndArray() override;
    };

    //
    // Translation of a log line to/from JSON
    //
    struct TlogLine {
        static constexpr const char *TS = {"TS"};
        Timestamp timestamp{};
        static constexpr const char *TP = {"TP"};
        std::vector<std::string> topicPath{};
        static constexpr const char *W = {"W"};
        WhatHappened action{WhatHappened::never};
        static constexpr const char *V = {"V"};
        data::StructElement value{};

        void serialize(
            const scope::UsingContext &context, rapidjson::Writer<rapidjson::StringBuffer> &writer);
        bool deserialize(const scope::UsingContext &context, std::ifstream &stream);

        static TlogLine readRecord(const scope::UsingContext &context, std::ifstream &stream);
        static WhatHappened decodeWhatHappened(std::string_view whatHappenedString);
    };

} // namespace config
