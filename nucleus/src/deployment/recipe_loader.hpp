#pragma once
#include "plugin.hpp"
#include <yaml-cpp/yaml.h>

namespace deployment {
    using ValueType = std::variant<bool, uint64_t, double, std::string, ggapi::Struct>;

    // TODO: Refactor into different namespace
    class RecipeLoader {
    public:
        RecipeLoader() = default;
        ggapi::Struct read(const std::filesystem::path &path) {
            std::ifstream stream{path};
            stream.exceptions(std::ios::failbit | std::ios::badbit);
            if(!stream.is_open()) {
                throw std::runtime_error("Unable to read config file");
            }
            return read(stream);
        }
        ggapi::Struct read(std::istream &stream) {
            YAML::Node root = YAML::Load(stream);
            return begin(root);
        }
        ggapi::Struct begin(YAML::Node &node) {
            auto rootStruct = ggapi::Struct::create();
            inplaceMap(rootStruct, node);
            return rootStruct;
        }

        // NOLINTNEXTLINE(*-no-recursion)
        ValueType rawValue(YAML::Node &node) {
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
        ggapi::Struct rawSequenceValue(YAML::Node &node) {
            auto child = ggapi::Struct::create();
            int idx = 0;
            for(auto i : node) {
                inplaceTopicValue(child, std::to_string(idx++), rawValue(i));
            }
            return child;
        }

        // NOLINTNEXTLINE(*-no-recursion)
        ggapi::Struct rawMapValue(YAML::Node &node) {
            auto data = ggapi::Struct::create();
            for(auto i : node) {
                auto key = i.first.as<std::string>();
                inplaceTopicValue(data, key, rawValue(i.second));
            }
            return data;
        }

        // NOLINTNEXTLINE(*-no-recursion)
        void inplaceMap(ggapi::Struct &data, YAML::Node &node) {
            if(!node.IsMap()) {
                throw std::runtime_error("Expecting a map or sequence");
            }
            for(auto i : node) {
                auto key = i.first.as<std::string>();
                inplaceValue(data, key, i.second);
            }
        }

        // NOLINTNEXTLINE(*-no-recursion)
        void inplaceValue(ggapi::Struct &data, const std::string &key, YAML::Node &node) {
            switch(node.Type()) {
                case YAML::NodeType::Map:
                    nestedMapValue(data, key, node);
                    break;
                case YAML::NodeType::Sequence:
                case YAML::NodeType::Scalar:
                case YAML::NodeType::Null:
                    inplaceTopicValue(data, key, rawValue(node));
                    break;
                default:
                    // ignore anything else
                    break;
            }
        }

        // NOLINTNEXTLINE(*-no-recursion)
        void inplaceTopicValue(ggapi::Struct &data, const std::string &key, const ValueType &vt) {
            std::visit(
                [key, &data](auto &&arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr(std::is_same_v<T, bool>) {
                        data.put(key, arg);
                    }
                    if constexpr(std::is_same_v<T, uint64_t>) {
                        data.put(key, arg);
                    }
                    if constexpr(std::is_same_v<T, double>) {
                        data.put(key, arg);
                    }

                    if constexpr(std::is_same_v<T, std::string>) {
                        data.put(key, arg);
                    }

                    if constexpr(std::is_same_v<T, ggapi::Struct>) {
                        data.put(key, arg);
                    }
                },
                vt);
        }

        // NOLINTNEXTLINE(*-no-recursion)
        void nestedMapValue(ggapi::Struct &data, const std::string &key, YAML::Node &node) {
            auto child = ggapi::Struct::create();
            data.put(key, child);
            inplaceMap(child, node);
        }
    };
} // namespace deployment
