#pragma once
#include "scope/context_full.hpp"
#include "data/shared_queue.hpp"
#include "conv/serializable.hpp"
#include "conv/yaml_conv.hpp"
#include "conv/yaml_serializer.hpp"
#include "deployment/recipe_model.hpp"

namespace config {
    template<typename Test, template<typename...> class Ref>
    struct is_specialization : std::false_type {};

    template<template<typename...> class Ref, typename... Args>
    struct is_specialization<Ref<Args...>, Ref>: std::true_type {};

    using IteratorType = YAML::const_iterator;

    class Iterator {
        size_t _itSize;
        bool _ignoreKeyCase = false;
    protected:
        size_t _itIndex{0};
        IteratorType _begin;
        IteratorType _end;
        IteratorType _current;

    public:
        virtual ~Iterator() = default;
        Iterator(const Iterator &other) = delete;
        Iterator(Iterator &&) = default;
        Iterator &operator=(const Iterator &other) = default;
        Iterator &operator=(Iterator &&) = default;
        explicit Iterator(const IteratorType& begin, const IteratorType &end): _begin(begin), _current(begin), _end(end), _itSize(std::distance(begin, end)) {

        }

        [[nodiscard]] size_t size() const {
            return _itSize;
        }

        Iterator &operator++() {
            _itIndex++;
            return *this;
        }

        void traverse() {
            if (_itIndex >= size()) {
                throw std::runtime_error("");
            }
            size_t idx = _itIndex;
            _current = _begin;
            while (idx > 0) {
                _current++;
                idx--;
            }
        }

        void setIgnoreKeyCase(bool ignoreCase = true) {
            _ignoreKeyCase = ignoreCase;
        }

        virtual YAML::Node find(const std::string &name) = 0;

        [[nodiscard]] virtual std::string name() = 0;

        [[nodiscard]] inline bool compareKeys( std::string key, std::string name) const {
            if(_ignoreKeyCase) {
                key = util::lower(key);
                name = util::lower(name);
            }
            return key == name;
        }

    };

    class MapIterator: public Iterator {

    public:
        explicit MapIterator(const IteratorType& begin, const IteratorType &end): Iterator(begin, end) {

        }

        YAML::Node find(const std::string &name) override {
            // do not care about index
            for(auto it = _begin; it != _end; it++) {
                auto key = it->first.as<std::string>();
                if(compareKeys(key, name)) {
                    return it->second;
                }
            }
            return {};
        }

        [[nodiscard]] std::string name() override
        {
            traverse();
            return _current->first.as<std::string>();
        }
    };

    class SequenceIterator: public Iterator {

    public:
        explicit SequenceIterator(const IteratorType& begin, const IteratorType &end): Iterator(begin, end) {

        }

        YAML::Node find(const std::string &name) override {
            traverse();
            auto node = *_current;
            for(_current = node.begin(); _current != node.end(); _current++) {
                auto key = _current->first.as<std::string>();
                if(compareKeys(key, name)) {
                    return _current->second;
                }
            }
            return {};
        }

        [[nodiscard]] std::string name() override
        {
            traverse();
            auto node = *_current;
            return node.as<std::string>();
        }
    };

    // TODO: Remove archive inheritance?
    class YamlRecipeReader: public conv::Archive, private scope::UsesContext {
        std::vector<std::unique_ptr<Iterator>> _stack;

    public:
        explicit YamlRecipeReader(const scope::UsingContext &context): scope::UsesContext{context} {
        }

        template <typename T>
        inline YamlRecipeReader& operator()( T && arg )
        {
            process( std::forward<T>( arg ));
            return *this;
        }

        template<typename T>
        inline YamlRecipeReader& operator()(const std::string&key, T&&arg) {
            process( key, std::forward<T>( arg ));
            return *this;
        }

        template<typename T>
        inline YamlRecipeReader& operator()(std::pair<std::string, T>&arg) {
            arg.first = _stack.back()->name();
            inplaceMap(_stack.back()->find(arg.first));
            apply(*this, arg.second);
            _stack.pop_back();
            ++(*_stack.back());
            return *this;
        }

//        template <class ... Types>
//        inline YamlRecipeReader& operator()( Types && ... args )
//        {
//            process( std::forward<Types>( args )... );
//            return *this;
//        }

        void read(const std::filesystem::path &path) {
            std::ifstream stream{path};
            stream.exceptions(std::ios::failbit | std::ios::badbit);
            if(!stream.is_open()) {
                throw std::runtime_error("Unable to read config file");
            }
            read(stream);
        }

        void read(std::ifstream &stream) {
            YAML::Node node = YAML::Load(stream);
            return inplaceMap(node);
        }

        void inplaceMap(const YAML::Node &node) {
            if(!(node.IsMap() || node.IsSequence())) {
                throw std::runtime_error("Expecting a map or sequence");
            }
            if (node.IsMap()) {
                _stack.emplace_back(std::make_unique<MapIterator>(node.begin(), node.end()));
            }
            else {
                _stack.emplace_back(std::make_unique<SequenceIterator>(node.begin(), node.end()));
            }
            _stack.back()->setIgnoreKeyCase(_ignoreKeyCase);
        }

        template<typename T>
        inline void process(T &head) {
            load(head);
        }

        template<typename T>
        inline void process(const std::string& key, T &head) {
            if constexpr(std::is_base_of_v<conv::Serializable, T>) {
                start(key);
                apply(*this, head);
                end();
            }
            else if constexpr(is_specialization<T, std::vector>::value) {
                start(key);
                load(key, head);
                end();
            }
            else if constexpr(is_specialization<T, std::unordered_map>::value) {
                start(key);
                load(head);
                end();
            }
            else {
                load(key, head);
            }
        }


//        template<typename T, typename... O>
//        inline void process(T &&head, O &&...tail ) {
//            process(std::forward<T>(head));
//            process(std::forward<O>(tail)...);
//        }

        template<typename ArchiveType, typename T>
//        template<typename T, typename = std::enable_if_t<std::is_base_of_v<conv::Serializable, T>>>
        void apply(ArchiveType &ar, T &head) {
            head.serialize(ar);
        }

        template<typename T>
        void load(T & head) {
            static_assert(std::is_base_of_v<conv::Serializable, T>);
            apply(*this, head);
        }

        template<typename T, typename = std::enable_if_t<std::is_base_of_v<conv::Serializable, T>>>
        void load(const std::string &key, std::vector<T> & head) {
            head.resize(_stack.back()->size());
           for(auto && v : head) {
                (*this)(v);
           }
        }

        template<typename T, typename = std::enable_if_t<std::is_base_of_v<conv::Serializable, T>>>
        void load(std::unordered_map<std::string, T> & head) {
            head.clear();

            auto hint = head.begin();
            for(auto i = 0; i < _stack.back()->size(); i++) {
                std::string key{};
                T value{};
                auto kv = std::make_pair(key, value);
                (*this)(kv);
                head.emplace_hint(hint, kv);
            }
        }

        template<typename T>
        //        template<typename T, typename std::enable_if_t<!std::is_base_of_v<conv::Serializable, std::decay_t<T>>>::value>
        void load(const std::string &key, T&data) {
            auto node = _stack.back()->find(key);
            // if not scalar then ignore
            if (node.IsScalar()) {
                if constexpr(std::is_same_v<T, bool>) {
                    data = node.as<bool>();
                }
                if constexpr(std::is_integral_v<T>) {
                    data = node.as<int>();
                }
                if constexpr(std::is_floating_point_v<T>) {
                    data = node.as<double>();
                }
                else {
                    data = node.as<std::string>();
                }
            }
        }


        void start(const std::string &key) {
            inplaceMap(_stack.back()->find(key));
        }

        void end() {
            _stack.pop_back();
            ++(*_stack.back());
        }

        // NOLINTNEXTLINE(*-no-recursion)
        data::ValueType rawValue(YAML::Node &node) {
            switch(node.Type()) {
                case YAML::NodeType::Map:
                    return rawMapValue(node);
                case YAML::NodeType::Sequence:
                    return rawSequenceValue(node);
                case YAML::NodeType::Scalar:
                    return node.as<std::string>();
                default:
                    break;
            }
            return {};
        }

        // NOLINTNEXTLINE(*-no-recursion)
        data::ValueType rawSequenceValue(YAML::Node &node) {
            auto newList{std::make_shared<data::SharedList>(scope::context())};
            int idx = 0;
            for(auto i : node) {
                newList->put(idx++, data::StructElement(rawValue(i)));
            }
            return newList;
        }

        // NOLINTNEXTLINE(*-no-recursion)
        data::ValueType rawMapValue(YAML::Node &node) {
            auto newMap{std::make_shared<data::SharedStruct>(scope::context())};
            for(auto i : node) {
                auto key = util::lower(i.first.as<std::string>());
                newMap->put(key, data::StructElement(rawValue(i.second)));
            }
            return newMap;
        }

        void inplaceMap(std::shared_ptr<data::SharedStruct> &data, YAML::Node &node) {
            if(!node.IsMap()) {
                throw std::runtime_error("Expecting a map or sequence");
            }
            for(auto i : node) {
                auto key = util::lower(i.first.as<std::string>());
                inplaceValue(data, key, i.second);
            }
        }

        void inplaceValue(
            std::shared_ptr<data::SharedStruct> &data, const std::string &key, YAML::Node &node) {
            switch(node.Type()) {
                case YAML::NodeType::Map:
                    nestedMapValue(data, key, node);
                    break;
                case YAML::NodeType::Sequence:
                case YAML::NodeType::Scalar:
                case YAML::NodeType::Null:
                    data->put(key, rawValue(node));
                    break;
                default:
                    // ignore anything else
                    break;
            }
        }

        void inplaceTopicValue(
            std::shared_ptr<data::SharedStruct> &data,
            const std::string &key,
            const data::ValueType &vt);

        void nestedMapValue(
            std::shared_ptr<data::SharedStruct> &data, const std::string &key, YAML::Node &node);
    };
} // namespace config
