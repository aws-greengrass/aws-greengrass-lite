#pragma once

#include "data/shared_list.hpp"
#include "data/struct_model.hpp"
#include <rapidjson/reader.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace conv {
    class JsonReader;

    enum class JsonState {
        Any,
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
            JsonReader &reader, const std::shared_ptr<data::StructModelBase> &target, bool started)
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
            JsonReader &reader, const std::shared_ptr<data::ListModelBase> &target, bool started)
            : JsonArrayResponder(reader, started), _target(target) {
        }

        bool parseValue(data::StructElement value) override;
        data::StructElement buildValue() override;
    };

    class JsonElementResponder : public JsonResponder {
    protected:
        data::StructElement &_value;

    public:
        JsonElementResponder(JsonReader &reader, data::StructElement &value)
            : JsonResponder(reader), _value(value) {
        }

        bool parseValue(data::StructElement value) override;
        bool parseKey(const std::string_view &view) override;
        bool parseStartObject() override;
        bool parseStartArray() override;
        bool parseEndObject() override;
        bool parseEndArray() override;
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
        std::shared_ptr<scope::Context> _context;
        std::vector<std::unique_ptr<JsonResponder>> _responders;

    public:
        explicit JsonReader(const std::shared_ptr<scope::Context> &context) : _context(context) {
        }

        [[nodiscard]] rapidjson::ParseResult read(std::ifstream &stream);
        [[nodiscard]] rapidjson::ParseResult readStream(std::istream &stream);

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

        std::shared_ptr<scope::Context> refContext() {
            return _context;
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

    struct JsonHelper {

        static std::shared_ptr<data::SharedBuffer> serializeToBuffer(
            const std::shared_ptr<scope::Context> &context,
            const std::shared_ptr<data::TrackedObject> &obj);

        static void serialize(
            const std::shared_ptr<scope::Context> &context,
            rapidjson::Writer<rapidjson::StringBuffer> &writer,
            const data::StructElement &value);
        static void serialize(
            const std::shared_ptr<scope::Context> &context,
            rapidjson::Writer<rapidjson::StringBuffer> &writer,
            std::monostate);
        static void serialize(
            const std::shared_ptr<scope::Context> &context,
            rapidjson::Writer<rapidjson::StringBuffer> &writer,
            bool b);
        static void serialize(
            const std::shared_ptr<scope::Context> &context,
            rapidjson::Writer<rapidjson::StringBuffer> &writer,
            uint64_t i);
        static void serialize(
            const std::shared_ptr<scope::Context> &context,
            rapidjson::Writer<rapidjson::StringBuffer> &writer,
            double d);
        static void serialize(
            const std::shared_ptr<scope::Context> &context,
            rapidjson::Writer<rapidjson::StringBuffer> &writer,
            const std::string &str);
        static void serialize(
            const std::shared_ptr<scope::Context> &context,
            rapidjson::Writer<rapidjson::StringBuffer> &writer,
            const data::Symbol &sym);
        static void serialize(
            const std::shared_ptr<scope::Context> &context,
            rapidjson::Writer<rapidjson::StringBuffer> &writer,
            const std::shared_ptr<data::TrackedObject> &obj);
    };
} // namespace conv
