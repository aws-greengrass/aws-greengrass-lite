#pragma once
#include "config_manager.hpp"
#include "util/commitable_file.hpp"
#include <fstream>
#include <yaml-cpp/yaml.h>

namespace config {

    class YamlReader {

    private:
        data::Environment &_environment;
        std::shared_ptr<Topics> _target;
        Timestamp _timestamp;

    public:
        explicit YamlReader(
            data::Environment &environment,
            const std::shared_ptr<Topics> &target,
            const Timestamp &timestamp
        )
            : _environment{environment}, _target{target}, _timestamp(timestamp) {
        }

        void read(const std::filesystem::path &path);
        void read(std::ifstream &stream);

        data::ValueType rawValue(YAML::Node &node);
        data::ValueType rawMapValue(YAML::Node &node);
        data::ValueType rawSequenceValue(YAML::Node &node);

        void inplaceMap(const std::shared_ptr<Topics> &topics, YAML::Node &node);

        void inplaceValue(
            const std::shared_ptr<Topics> &topics, const std::string &key, YAML::Node &node
        );

        void inplaceTopicValue(
            const std::shared_ptr<Topics> &topics, const std::string &key, const data::ValueType &vt
        );

        void nestedMapValue(
            const std::shared_ptr<Topics> &topics, const std::string &key, YAML::Node &node
        );
    };

    struct YamlHelper {
        static void serialize(
            data::Environment &environment, YAML::Emitter &emitter, const data::StructElement &value
        );
        static void serialize(
            data::Environment &environment,
            YAML::Emitter &emitter,
            const std::shared_ptr<Topics> &value
        );
        static void write(
            data::Environment &environment,
            util::CommitableFile &path,
            const std::shared_ptr<Topics> &node
        );
        static void write(
            data::Environment &environment,
            std::ofstream &stream,
            const std::shared_ptr<Topics> &node
        );
    };
} // namespace config
