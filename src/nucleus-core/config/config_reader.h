#pragma once
#include "config_manager.h"
#include <yaml-cpp/yaml.h>

namespace config {

    class YamlReader {
        class Node;

    private:
        std::shared_ptr<Topics> _target;
        Timestamp _timestamp;

    public:
        explicit YamlReader(const std::shared_ptr<Topics> &target, const Timestamp &timestamp)
            : _target{target}, _timestamp(timestamp) {
        }

        void read(const std::filesystem::path &path);

        void inplaceMap(const std::shared_ptr<Topics> &topics, YAML::Node &node);

        void inplaceValue(
            const std::shared_ptr<Topics> &topics, const std::string &key, YAML::Node &node
        );

        void inplaceSequenceValue(
            const std::shared_ptr<Topics> &topics, const std::string &key, YAML::Node &node
        );

        void inplaceScalarValue(
            const std::shared_ptr<Topics> &topics, const std::string &key, YAML::Node &node
        );

        void inplaceNullValue(const std::shared_ptr<Topics> &topics, const std::string &key);

        void nestedMapValue(
            const std::shared_ptr<Topics> &topics, const std::string &key, YAML::Node &node
        );
    };
} // namespace config
