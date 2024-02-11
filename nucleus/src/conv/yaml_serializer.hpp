#pragma once
#include "data/shared_list.hpp"
#include "data/shared_struct.hpp"
#include "serializable.hpp"
#include <memory>
#include <yaml-cpp/yaml.h>

namespace conv {
    class YamlMapDeserializer : public Archive {

    public:
        YamlMapDeserializer() = default;

        data::StructElement save(YAML::Node &node);

        data::StructElement decode(YAML::Node &node) const;

        std::unique_ptr<Archive> tryGetKey(std::string_view sv) const;
    };

    class YamlSequenceDeserializer : public Archive {
        YAML::Node _node;

    public:
        explicit YamlSequenceDeserializer(const YAML::Node &node);

        void save(std::shared_ptr<data::StructModelBase> &objectStruct);

        std::unique_ptr<Archive> decode(YAML::Node &node) const;

        std::unique_ptr<Archive> tryGetKey(std::string_view sv) const;
    };

    class YamlScalarDeserializer : public Archive {
        YAML::Node _node;

    public:
        explicit YamlScalarDeserializer(const YAML::Node &node);

        void save(std::shared_ptr<data::StructModelBase> &objectStruct);

        std::unique_ptr<Archive> decode(YAML::Node &node) const;

        std::unique_ptr<Archive> tryGetKey(std::string_view sv) const;
    };

    class YamlNullDeserializer : public Archive {
        YAML::Node _node;

    public:
        explicit YamlNullDeserializer(const YAML::Node &node);

        data::StructElement save(YAML::Node &node);

        data::StructElement decode(YAML::Node &node) const;

        std::unique_ptr<Archive> tryGetKey(std::string_view sv) const;
    };
} // namespace conv
