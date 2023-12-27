#pragma once
#include "conv/yaml_conv.hpp"
#include "scope/context.hpp"

namespace config {
    class YamlConfigReader : public conv::YamlReaderBase {
        std::shared_ptr<config::Topics> _target;
        config::Timestamp _timestamp;

    public:
        explicit YamlConfigReader(
            const scope::UsingContext &context,
            const std::shared_ptr<config::Topics> &target,
            const config::Timestamp &timestamp)
            : YamlReaderBase(context), _target{target}, _timestamp(timestamp) {
        }

        void begin(YAML::Node &node) override;

        void inplaceMap(const std::shared_ptr<config::Topics> &topics, YAML::Node &node);

        void inplaceValue(
            const std::shared_ptr<config::Topics> &topics,
            const std::string &key,
            YAML::Node &node);

        void inplaceTopicValue(
            const std::shared_ptr<config::Topics> &topics,
            const std::string &key,
            const data::ValueType &vt);

        void nestedMapValue(
            const std::shared_ptr<config::Topics> &topics,
            const std::string &key,
            YAML::Node &node);
    };

    struct YamlConfigHelper {
        static void serialize(
            const scope::UsingContext &context,
            YAML::Emitter &emitter,
            const std::shared_ptr<config::Topics> &value);
        static void write(
            const scope::UsingContext &context,
            util::CommitableFile &path,
            const std::shared_ptr<config::Topics> &node);
        static void write(
            const scope::UsingContext &context,
            std::ofstream &stream,
            const std::shared_ptr<config::Topics> &node);
    };

} // namespace config
