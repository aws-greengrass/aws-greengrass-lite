#pragma once
#include "data/object_model.hpp"
#include "data/serializable.hpp"
#include "scope/context_full.hpp"
#include "util.hpp"
#include <conv/json_conv.hpp>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>

namespace config {
    using MemberIterator = rapidjson::GenericValue<rapidjson::UTF8<>>::ConstMemberIterator;
    using ValueIterator = rapidjson::GenericValue<rapidjson::UTF8<>>::ConstValueIterator;
    using GenericValue = rapidjson::Document::GenericValue;

    class JsonDeserializer : private scope::UsesContext {

        class Iterator {
            size_t _itSize;
            long _itIndex{0};
            bool _ignoreKeyCase = false;
            MemberIterator _itMemberItBegin{}, _itMemberItEnd{};
            ValueIterator _itValueItBegin{}, _itValueItEnd{};
            enum Type { Value, Member, Null } _itType{Null};

        public:
            Iterator(const Iterator &other) = delete;
            Iterator(Iterator &&) = default;
            Iterator &operator=(const Iterator &other) = default;
            Iterator &operator=(Iterator &&) = default;

            explicit Iterator(MemberIterator begin, MemberIterator end)
                : _itMemberItBegin(begin), _itMemberItEnd(end), _itSize(std::distance(begin, end)),
                  _itType(Member) {
                if(_itSize == 0) {
                    _itType = Null;
                }
            }

            explicit Iterator(ValueIterator begin, ValueIterator end)
                : _itValueItBegin(begin), _itValueItEnd(end), _itSize(std::distance(begin, end)),
                  _itType(Value) {
                if(_itSize == 0) {
                    _itType = Null;
                }
            }

            [[nodiscard]] size_t size() const {
                return _itSize;
            }

            Iterator &operator++() {
                _itIndex++;
                return *this;
            }

            void setIgnoreKeyCase(bool ignoreCase = true) {
                _ignoreKeyCase = ignoreCase;
            }

            bool find(const std::string &name) {
                long index = 0;
                for(auto it = _itMemberItBegin; it != _itMemberItEnd; ++it, ++index) {
                    const auto currentName = it->name.GetString();
                    if(compareKeys(currentName, name)) {
                        _itIndex = index;
                        return true;
                    }
                }
                return false;
            }

            [[nodiscard]] const char *name() const {
                if(_itType == Member && (_itMemberItBegin + _itIndex) != _itMemberItEnd) {
                    return _itMemberItBegin[_itIndex].name.GetString();
                }
                return nullptr;
            }

            [[nodiscard]] const GenericValue &value() {
                if(_itIndex >= _itSize) {
                    throw std::runtime_error("Index out of bounds");
                }
                switch(_itType) {
                    case Value:
                        return _itValueItBegin[_itIndex];
                    case Member:
                        return _itMemberItBegin[_itIndex].value;
                    default:
                        throw std::runtime_error("Unknown");
                }
            }

            [[nodiscard]] inline bool compareKeys(std::string key, std::string name) const {
                if(_ignoreKeyCase) {
                    key = util::lower(key);
                    name = util::lower(name);
                }
                return key == name;
            }
        };

        std::vector<Iterator> _stack;
        rapidjson::Document _rootDoc;

    public:
        explicit JsonDeserializer(const scope::UsingContext &context)
            : scope::UsesContext{context} {
        }

        template<typename T>
        inline JsonDeserializer &operator()(T &&arg) {
            process(std::forward<T>(arg));
            return *this;
        }

        template<typename T>
        inline JsonDeserializer &operator()(const std::string &key, T &&arg) {
            process(key, std::forward<T>(arg));
            return *this;
        }

        template<typename T, typename = std::enable_if_t<std::is_base_of_v<conv::Serializable, T>>>
        inline JsonDeserializer &operator()(std::pair<std::string, T> &arg) {
            arg.first = _stack.back().name();
            arg.first = util::lower(arg.first);
            if(_stack.back().find(arg.first)) {
                inplaceMap();
            }
            process(arg.second);
            _stack.pop_back();
            ++(_stack.back());
            return *this;
        }

        template<typename T>
        inline JsonDeserializer &operator()(std::pair<std::string, T> &arg) {
            arg.first = _stack.back().name();
            process(arg.second);
            ++(_stack.back());
            return *this;
        }

        inline JsonDeserializer &operator()(std::pair<std::string, data::Object> &arg) {
            arg.first = _stack.back().name();
            arg.first = util::lower(arg.first);
            if(_stack.back().find(arg.first)) {
                inplaceMap();
            }
            if(_stack.back().value().IsNull()) {
                arg.second = _stack.back().value().GetString();
            } else {
                // TODO: Not implemented
                throw std::runtime_error("Not implemented");
            }
            ++(_stack.back());
            return *this;
        }

        void read(const std::filesystem::path &path) {
            std::ifstream stream{path};
            if(!stream.is_open()) {
                throw std::runtime_error("Unable to read config file");
            }
            read(stream);
        }

        void read(std::ifstream &stream) {
            // TODO: clear stack?
            _stack.clear();
            rapidjson::IStreamWrapper istream{stream};
            _rootDoc.ParseStream(istream);

            if(!(_rootDoc.IsArray() || _rootDoc.IsObject())) {
                throw std::runtime_error("Invalid json format. Expecting a map or array");
            }
            if(_rootDoc.IsArray()) {
                _stack.emplace_back(_rootDoc.Begin(), _rootDoc.End());
            } else {
                _stack.emplace_back(_rootDoc.MemberBegin(), _rootDoc.MemberEnd());
            }
        }

        void read(const std::string &jsonString) {
            // TODO: clear stack?
            _stack.clear();
            rapidjson::StringStream sstream{jsonString.c_str()};
            _rootDoc.ParseStream(sstream);

            if(!(_rootDoc.IsArray() || _rootDoc.IsObject())) {
                throw std::runtime_error("Invalid json format. Expecting a map or array");
            }
            if(_rootDoc.IsArray()) {
                _stack.emplace_back(_rootDoc.Begin(), _rootDoc.End());
            } else {
                _stack.emplace_back(_rootDoc.MemberBegin(), _rootDoc.MemberEnd());
            }
        }

        bool inplaceMap() {
            if(!(_stack.back().value().IsArray() || _stack.back().value().IsObject())) {
                return false; // optional array or object
            }
            if(_stack.back().value().IsArray()) {
                _stack.emplace_back(_stack.back().value().Begin(), _stack.back().value().End());
            } else {
                _stack.emplace_back(
                    _stack.back().value().MemberBegin(), _stack.back().value().MemberEnd());
            }
            return true;
        }

        template<typename T>
        std::enable_if_t<std::is_base_of_v<conv::Serializable, T>> inline process(T &head) {
            load(head);
        }

        inline void process(std::string &head) {
            rawValue(head);
        }

        template<typename T>
        inline void process(const std::string &key, T &head) {
            // TODO: overload instead
            if constexpr(util::is_specialization<T, std::vector>::value) {
                auto exists = start(key);
                if(exists) {
                    load(key, head);
                }
            } else if constexpr(util::is_specialization<T, std::unordered_map>::value) {
                auto exists = start(key);
                if(exists) {
                    load(head);
                    end();
                }
            } else if constexpr(std::is_base_of_v<data::StructModelBase, T>) {
                auto exists = start(key);
                if(exists) {
                    load(head);
                    end();
                }
            } else if constexpr(std::is_base_of_v<conv::Serializable, T>) {
                auto exists = start(key);
                if(exists) {
                    load(head);
                    end();
                }
            } else {
                load(key, head);
            }
        }

        template<typename ArchiveType, typename T>
        void apply(ArchiveType &ar, T &head) {
            head.serialize(ar);
        }

        template<typename T>
        void load(T &head) {
            apply(*this, head);
        }

        template<
            typename T,
            typename = std::enable_if_t<std::is_base_of_v<data::StructModelBase, T>>>
        void load(std::shared_ptr<T> &head) {
            // TODO: Not implemented
            throw std::runtime_error("Not implemented");
        }

        template<typename T>
        std::enable_if_t<std::is_base_of_v<conv::Serializable, T>> load(
            const std::string &key, std::vector<T> &head) {
            head.resize(_stack.back().size());
            for(auto &&v : head) {
                inplaceMap();
                (*this)(v);
                _stack.pop_back();
                ++(_stack.back());
            }
            end();
        }

        template<typename T>
        std::enable_if_t<!std::is_base_of_v<conv::Serializable, T>> load(
            const std::string &key, std::vector<T> &head) {
            head.resize(_stack.back().size());
            for(auto &v : head) {
                rawValue(v);
                ++(_stack.back());
            }
            _stack.pop_back();
        }

        template<typename T>
        void load(std::unordered_map<std::string, T> &head) {
            head.clear();

            auto hint = head.begin();
            for(auto i = 0; i < _stack.back().size(); i++) {
                std::string key{};
                T value{};
                auto kv = std::make_pair(key, value);
                (*this)(kv);
                head.emplace_hint(hint, kv);
            }
        }

        template<typename T>
        void load(const std::string &key, T &data) {
            if(_stack.back().find(key)) {
                rawValue(data);
            }
        }

        bool start(const std::string &key) {
            if(_stack.back().find(key)) {
                return inplaceMap();
            }
            return false;
        }

        void end() {
            _stack.pop_back();
            ++(_stack.back());
        }

        // NOLINTNEXTLINE(*-no-recursion)
        template<typename T>
        void rawValue(T &data) {
            // TODO: Check types
            if constexpr(std::is_same_v<T, bool>) {
                data = _stack.back().value().GetBool();
            } else if constexpr(std::is_integral_v<T>) {
                data = _stack.back().value().GetUint64();
            } else if constexpr(std::is_floating_point_v<T>) {
                data = _stack.back().value().GetDouble();
            } else {
                data = _stack.back().value().GetString();
            }
        }
    };
} // namespace config
