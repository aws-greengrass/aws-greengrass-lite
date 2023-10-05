#pragma once

#include "config_manager.h"
#include "data/shared_list.h"
#include "data/struct_model.h"
#include <rapidjson/reader.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace config {
    class JsonReader;

    enum class JsonState {
        Any,
        Completed,
        ExpectValue,
        ExpectStartObject,
        ExpectStartArray,
        ExpectKey,
        Error
    };

    class JsonResponder {
    protected:
        JsonReader &_reader;

    public:
        explicit JsonResponder(JsonReader &reader) : _reader(reader) {
        }

        JsonResponder(const JsonResponder &) = delete;
        JsonResponder(JsonResponder &&) = delete;
        JsonResponder &operator=(const JsonResponder &) = delete;
        JsonResponder &operator=(JsonResponder &&) = delete;
        virtual ~JsonResponder() = default;

        virtual bool parseValue(data::StructElement value) = 0;
        virtual bool parseKey(const std::string_view &) = 0;
        virtual bool parseStartObject() = 0;
        virtual bool parseEndObject() = 0;
        virtual bool parseStartArray() = 0;
        virtual bool parseEndArray() = 0;
    };

    class JsonStructResponder : public JsonResponder {
    protected:
        JsonState _state;
        std::string _key;

    public:
        JsonStructResponder(JsonReader &reader, bool started)
            : JsonResponder(reader),
              _state(started ? JsonState::ExpectKey : JsonState::ExpectStartObject) {
        }

        virtual bool parseKeyValue(const std::string &key, data::StructElement value) = 0;

        virtual data::StructElement buildValue() {
            return {};
        }

        bool parseValue(data::StructElement value) override;
        bool parseKey(const std::string_view &key) override;
        bool parseStartObject() override;
        bool parseEndObject() override;
        bool parseStartArray() override;
        bool parseEndArray() override;
    };

    class JsonArrayResponder : public JsonResponder {
    protected:
        JsonState _state;

    public:
        JsonArrayResponder(JsonReader &reader, bool started)
            : JsonResponder(reader),
              _state(started ? JsonState::ExpectValue : JsonState::ExpectStartArray) {
        }

        virtual data::StructElement buildValue() {
            return {};
        }

        bool parseKey(const std::string_view &key) override;
        bool parseStartObject() override;
        bool parseEndObject() override;
        bool parseStartArray() override;
        bool parseEndArray() override;
    };

    class JsonSharedStructResponder : public JsonStructResponder {
    protected:
        std::shared_ptr<data::StructModelBase> _target;

    public:
        JsonSharedStructResponder(
            JsonReader &reader, const std::shared_ptr<data::StructModelBase> &target, bool started
        )
            : JsonStructResponder(reader, started), _target(target) {
        }

        bool parseKeyValue(const std::string &key, data::StructElement value) override;
        data::StructElement buildValue() override;
    };

    class JsonSharedListResponder : public JsonArrayResponder {
    protected:
        int32_t _idx{0};
        std::shared_ptr<data::ListModelBase> _target;

    public:
        JsonSharedListResponder(
            JsonReader &reader, const std::shared_ptr<data::ListModelBase> &target, bool started
        )
            : JsonArrayResponder(reader, started), _target(target) {
        }

        bool parseValue(data::StructElement value) override;
        data::StructElement buildValue() override;
    };

    class JsonStructValidator : public JsonStructResponder {
    public:
        explicit JsonStructValidator(JsonReader &reader, bool started)
            : JsonStructResponder(reader, started) {
        }

        bool parseKeyValue(const std::string &key, data::StructElement value) override {
            return true;
        }

        data::StructElement buildValue() override {
            return {};
        }
    };

    class JsonArrayValidator : public JsonArrayResponder {
    public:
        JsonArrayValidator(JsonReader &reader, bool started) : JsonArrayResponder(reader, started) {
        }

        bool parseValue(data::StructElement value) override {
            return true;
        }

        data::StructElement buildValue() override {
            return {};
        }
    };

    class JsonReader {

    private:
        data::Environment &_environment;
        std::vector<std::unique_ptr<JsonResponder>> _responders;

    public:
        explicit JsonReader(data::Environment &environment) : _environment{environment} {
        }

        [[nodiscard]] rapidjson::ParseResult read(std::ifstream &stream);

        void push(std::unique_ptr<JsonResponder> responder) {
            _responders.emplace_back(std::move(responder));
        }

        [[nodiscard]] bool nested() const {
            return !_responders.empty();
        }

        JsonResponder &top() {
            return *_responders.back();
        }

        bool pop(data::StructElement value) {
            _responders.pop_back();
            if(nested()) {
                top().parseValue(std::move(value));
            }
            return true;
        }

        data::Environment &environment() {
            return _environment;
        }

        bool Null() {
            data::StructElement el;
            return top().parseValue(el);
        }

        bool Bool(bool b) {
            data::StructElement el(b);
            return top().parseValue(el);
        }

        bool Int(int i) {
            return Uint64(static_cast<uint64_t>(i));
        }

        bool Uint(unsigned u) {
            return Uint64(static_cast<uint64_t>(u));
        }

        bool Int64(int64_t i) {
            return Uint64(static_cast<uint64_t>(i));
        }

        bool Uint64(uint64_t u) {
            data::StructElement el(u);
            return top().parseValue(el);
        }

        bool Double(double d) {
            data::StructElement el(d);
            return top().parseValue(el);
        }

        bool StartObject() {
            return top().parseStartObject();
        }

        bool EndObject(rapidjson::SizeType) {
            return top().parseEndObject();
        }

        bool StartArray() {
            return top().parseStartArray();
        }

        bool EndArray(rapidjson::SizeType) {
            return top().parseEndArray();
        }

        bool Key(const char *str, rapidjson::SizeType len, bool) {
            return top().parseKey(std::string_view(str, len));
        }

        bool String(const char *str, rapidjson::SizeType len, bool) {
            data::StructElement el(std::string(str, len));
            return top().parseValue(el);
        }

        bool RawNumber(const char *str, rapidjson::SizeType len, bool copy) {
            return String(str, len, copy);
        }
    };

    class TlogLine;

    class TlogLineResponder : public JsonStructResponder {
        TlogLine &_tlogLine;

    public:
        explicit TlogLineResponder(JsonReader &reader, TlogLine &line, bool started)
            : JsonStructResponder(reader, started), _tlogLine(line) {
        }

        bool parseKeyValue(const std::string &key, data::StructElement value) override;
        bool parseStartArray() override;
        bool parseStartObject() override;
    };

    class TlogLinePathResponder : public JsonArrayResponder {
        TlogLine &_tlogLine;
        std::vector<std::string> _path;

    public:
        explicit TlogLinePathResponder(JsonReader &reader, TlogLine &line, bool started)
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
            data::Environment &environment, rapidjson::Writer<rapidjson::StringBuffer> &writer
        );
        bool deserialize(data::Environment &environment, std::ifstream &stream);

        static TlogLine readRecord(data::Environment &environment, std::ifstream &stream);
        static WhatHappened decodeWhatHappened(std::string_view whatHappenedString);
    };

    struct JsonHelper {
        static void serialize(
            data::Environment &environment,
            rapidjson::Writer<rapidjson::StringBuffer> &writer,
            const data::StructElement &value
        );
    };
} // namespace config
