#pragma once

#include "config/config_manager.hpp"
#include "scope/context_full.hpp"
#include "util/commitable_file.hpp"
#include <fstream>
#include <yaml-cpp/yaml.h>

namespace conv {

    class YamlReaderBase : private scope::UsesContext {

    public:
        using scope::UsesContext::UsesContext;
        virtual ~YamlReaderBase() = default;

        void read(const std::filesystem::path &path);
        void read(std::istream &stream);
        virtual void begin(YAML::Node &node) = 0;

        data::ValueType rawValue(YAML::Node &node);
        data::ValueType rawMapValue(YAML::Node &node);
        data::ValueType rawSequenceValue(YAML::Node &node);
    };

    class YamlReader : public YamlReaderBase {
        std::shared_ptr<data::SharedStruct> _target;

    public:
        explicit YamlReader(
            const scope::UsingContext &context, const std::shared_ptr<data::SharedStruct> &target)
            : YamlReaderBase(context), _target{target} {
        }

        void begin(YAML::Node &node) override;

        void inplaceMap(std::shared_ptr<data::SharedStruct> &data, YAML::Node &node);

        void inplaceValue(
            std::shared_ptr<data::SharedStruct> &data, const std::string &key, YAML::Node &node);

        void nestedMapValue(
            std::shared_ptr<data::SharedStruct> &data, const std::string &key, YAML::Node &node);
    };

    struct YamlHelper {
        static std::shared_ptr<data::SharedBuffer> serializeToBuffer(
            const scope::UsingContext &context, const std::shared_ptr<data::TrackedObject> &obj);
        static void serialize(
            const scope::UsingContext &context,
            YAML::Emitter &emitter,
            const data::StructElement &value);
    };
} // namespace conv
