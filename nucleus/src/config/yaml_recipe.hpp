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

        using IteratorType = YAML::const_iterator;
        IteratorType _begin;
        IteratorType _end;
        size_t _itSize, _itIndex{0};

    public:
        explicit Iterator(IteratorType begin, IteratorType end): _begin(begin), _end(end), _itSize(std::distance(begin, end)) {

        }

        Iterator &operator++() {
            _itIndex++;
            return *this;
        }

        const YAML::Node& value() {
            if(_itIndex >= _itSize) {
                throw std::runtime_error("");
            }
            size_t idx = _itIndex;
            auto it = _begin;
            while (idx--) { // no index operator in yaml-cpp
                it++;
            }
            return it->second;
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

    class YamlRecipeReader: public conv::Archive {
        std::vector<IteratorType> _stack;

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

        YAML::Node find(std::string name) {
            for(auto& it: _stack) {
                auto key = it->first.as<std::string>();
                if(compareKeys(key, name)) {
                    return it->second;
                }
            }
            return YAML::Node();
        }

        IteratorType value(std::string name) {
            for(auto& it: _stack) {
                auto key = it->first.as<std::string>();
                if(compareKeys(key, name)) {
                    return it->second.begin();
                }
            }
            return YAML::Node().begin();
        }

        // NOLINTNEXTLINE(*-no-recursion)
        void inplaceMap(const YAML::Node &node) {
            if(!(node.IsMap() || node.IsSequence())) {
                throw std::runtime_error("Expecting a map or sequence");
            }
            for(auto it = node.begin(); it != node.end(); it++) {
                _stack.emplace_back(it);
            }
        }

        template<typename T>
        inline void process(const std::string& key, T &head) {
            if constexpr(std::is_base_of_v<conv::Serializable, T>) {
                inplaceMap(find(key));
                apply(*this, head);
                _stack.pop_back();
            }
            else if constexpr(is_specialization<T, std::vector>::value) {
                load(key, head);
            }
            else {
                load(key, head);
            }
        }

        template<typename T>
        inline void process(T &head) {
            process(std::forward<T>(head));

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



        template<typename T>
        //        template<typename T, typename std::enable_if_t<!std::is_base_of_v<conv::Serializable, std::remove_reference_t<T>>>::value>
        void load(const std::string &key, T&data) {
            auto node = find(key);
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

        template<typename T, typename = std::enable_if_t<std::is_base_of_v<conv::Serializable, T>>>
        void load(const std::string &key, std::vector<T> & head) {
            head.resize(find(key).size());
           for(auto && v : head) {
                (*this)(key, v);
           }
        }

        template<typename T, typename = std::enable_if_t<std::is_base_of_v<conv::Serializable, T>>>
        void load(const std::string &key, std::unordered_map<std::string, T> & head) {
            // TODO:
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
