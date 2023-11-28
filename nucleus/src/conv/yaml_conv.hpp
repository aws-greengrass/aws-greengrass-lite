#pragma once
#include "config/config_manager.hpp"
#include "util/commitable_file.hpp"
#include <fstream>
#include <yaml-cpp/yaml.h>

namespace conv {

    class YamlReaderBase {

        std::shared_ptr<scope::Context> _context;

    public:
        explicit YamlReaderBase(const std::shared_ptr<scope::Context> &context)
            : _context{context} {
        }
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
            const std::shared_ptr<scope::Context> &context,
            const std::shared_ptr<data::SharedStruct> &target)
            : YamlReaderBase(context), _target{target} {
        }

        void begin(YAML::Node &node);
    };

    struct YamlHelper {
        static void serialize(
            const std::shared_ptr<scope::Context> &context,
            YAML::Emitter &emitter,
            const data::StructElement &value);
    };
} // namespace conv
