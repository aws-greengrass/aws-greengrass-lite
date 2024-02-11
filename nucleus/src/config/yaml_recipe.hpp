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

    public:
        virtual ~Iterator() = default;
        explicit Iterator(const IteratorType& begin, const IteratorType &end): _begin(begin), _end(end), _itSize(std::distance(begin, end)) {

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

        virtual YAML::Node find(const std::string &name) = 0;

        [[nodiscard]] inline bool compareKeys( std::string key, std::string name) const {
            if(_ignoreKeyCase) {
                key = util::lower(key);
                name = util::lower(name);
            }
            return key == name;
        }

        [[nodiscard]] const char * name() const
        {
            size_t idx = _itIndex;
            auto it = _begin;
            while (idx--) {
                it++;
            }
            if(it->Type() == YAML::NodeType::Null) {
                return it->Scalar().c_str();
            }
            else {
                return nullptr;
            }
        }

    };

    class MapIterator: public Iterator {

    public:
        explicit MapIterator(const IteratorType& begin, const IteratorType &end): Iterator(begin, end) {

        }

        YAML::Node find(const std::string &name) override {
            for(auto it = _begin; it != _end; it++) {
                auto key = it->first.as<std::string>();
                if(compareKeys(key, name)) {
                    return it->second;
                }
            }
            return {};
        }
    };

    class SequenceIterator: public Iterator {

    public:
        explicit SequenceIterator(const IteratorType& begin, const IteratorType &end): Iterator(begin, end) {

        }

        YAML::Node find(const std::string &name) override {
            auto it = _begin;
            auto idx = _itIndex;
            if (_itIndex >= size()) {
                throw std::runtime_error("");
            }
            while (idx > 0 && it != _end) {
                it++;
                idx--;
            }
            _itIndex++;
            auto node = *it;
            for(it = node.begin(); it != node.end(); it++) {
                auto key = it->first.as<std::string>();
                if(compareKeys(key, name)) {
                    return it->second;
                }
            }
            return {};
        }
    };

    class YamlRecipeReader: public conv::Archive {
        std::vector<std::unique_ptr<Iterator>> _stack;

    public:
        explicit YamlRecipeReader(const scope::UsingContext &context) {
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

//        template <class ... Types>
//        inline YamlRecipeReader& operator()( Types && ... args )
//        {
//            process( std::forward<Types>( args )... );
//            return *this;
//        }

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
        inline void process(const std::string& key, T &head) {
            if constexpr(std::is_base_of_v<conv::Serializable, T>) {
                inplaceMap(_stack.back()->find(key));
                apply(*this, head);
                _stack.pop_back();
            }
            else if constexpr(is_specialization<T, std::vector>::value) {
                inplaceMap(_stack.back()->find(key));
                load(key, head);
                _stack.pop_back();
            }
            else {
                load(key, head);
            }
        }

        template<typename T>
        inline void process(T &head) {
            load(head);
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
        void load(const std::string &key, std::unordered_map<std::string, T> & head) {
            // TODO:
        }

        template<typename T>
        //        template<typename T, typename std::enable_if_t<!std::is_base_of_v<conv::Serializable, std::remove_reference_t<T>>>::value>
        void load(const std::string &key, T&data) {
            auto node = _stack.back()->find(key);
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


        void start() {

        }

        void end() {

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
