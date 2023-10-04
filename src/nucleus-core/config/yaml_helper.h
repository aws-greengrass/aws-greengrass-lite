#pragma once
#include "config_manager.h"
#include <yaml-cpp/yaml.h>

namespace config {

    class YamlReader {
        class Node;

    private:
        data::Environment & _environment;
        std::shared_ptr<Topics> _target;
        Timestamp _timestamp;

    public:
        explicit YamlReader(data::Environment & environment, const std::shared_ptr<Topics> &target, const Timestamp &timestamp) :
                _environment{environment}, _target{target}, _timestamp(timestamp) {
        }

        void read(const std::filesystem::path &path);

        data::ValueType rawValue(YAML::Node & node);
        data::ValueType rawMapValue(YAML::Node & node);
        data::ValueType rawSequenceValue(YAML::Node & node);

        void inplaceMap(const std::shared_ptr<Topics> & topics, YAML::Node & node);

        void inplaceValue(
            const std::shared_ptr<Topics> &topics, const std::string &key, YAML::Node &node
        );

        void inplaceTopicValue(const std::shared_ptr<Topics> & topics, const std::string & key, const data::ValueType & vt);

        void nestedMapValue(
            const std::shared_ptr<Topics> &topics, const std::string &key, YAML::Node &node
        );
    };
} // namespace config
