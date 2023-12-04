#include "yaml_config.hpp"
#include "data/shared_list.hpp"
#include "scope/context_full.hpp"
#include "util/commitable_file.hpp"
#include <memory>

namespace config {

    void YamlConfigReader::begin(YAML::Node &node) {
        inplaceMap(_target, node);
    }

    // NOLINTNEXTLINE(*-no-recursion)
    void YamlConfigReader::inplaceMap(
        const std::shared_ptr<config::Topics> &topics, YAML::Node &node) {
        if(!node.IsMap()) {
            throw std::runtime_error("Expecting a map");
        }
        for(auto i : node) {
            auto key = i.first.as<std::string>();
            inplaceValue(topics, key, i.second);
        }
    }

    // NOLINTNEXTLINE(*-no-recursion)
    void YamlConfigReader::inplaceValue(
        const std::shared_ptr<config::Topics> &topics, const std::string &key, YAML::Node &node) {
        switch(node.Type()) {
            case YAML::NodeType::Map:
                nestedMapValue(topics, key, node);
                break;
            case YAML::NodeType::Sequence:
            case YAML::NodeType::Scalar:
            case YAML::NodeType::Null:
                inplaceTopicValue(topics, key, rawValue(node));
                break;
            default:
                // ignore anything else
                break;
        }
    }

    void YamlConfigReader::inplaceTopicValue(
        const std::shared_ptr<config::Topics> &topics,
        const std::string &key,
        const data::ValueType &vt) {
        config::Topic topic = topics->createTopic(key, _timestamp);
        topic.withNewerValue(_timestamp, vt);
    }

    // NOLINTNEXTLINE(*-no-recursion)
    void YamlConfigReader::nestedMapValue(
        const std::shared_ptr<config::Topics> &topics, const std::string &key, YAML::Node &node) {
        std::shared_ptr<config::Topics> nested = topics->createInteriorChild(key, _timestamp);
        inplaceMap(nested, node);
    }

    void YamlConfigHelper::write(
        const std::shared_ptr<scope::Context> &context,
        util::CommitableFile &path,
        const std::shared_ptr<config::Topics> &node) {
        path.begin(std::ios_base::out | std::ios_base::trunc);
        std::ofstream &stream = path.getStream();
        stream.exceptions(std::ios::failbit | std::ios::badbit);
        write(context, stream, node);
        path.commit();
    }

    void YamlConfigHelper::write(
        const std::shared_ptr<scope::Context> &context,
        std::ofstream &stream,
        const std::shared_ptr<config::Topics> &node) {
        YAML::Emitter out;
        if(!stream.is_open()) {
            throw std::runtime_error("Unable to write config file");
        }
        serialize(context, out, node);
        stream << out.c_str();
    }

    // NOLINTNEXTLINE(*-no-recursion)
    void YamlConfigHelper::serialize(
        const std::shared_ptr<scope::Context> &context,
        YAML::Emitter &emitter,
        const std::shared_ptr<config::Topics> &node) {
        emitter << YAML::BeginMap;
        std::vector<config::Topic> leafs = node->getLeafs();
        for(const auto &i : leafs) {
            emitter << YAML::Key << i.getNameOrd().toString() << YAML::Value;
            conv::YamlHelper::serialize(context, emitter, i);
        }
        leafs.clear();
        std::vector<std::shared_ptr<config::Topics>> subTopics = node->getInteriors();
        for(const auto &i : subTopics) {
            emitter << YAML::Key << i->getName() << YAML::Value;
            serialize(context, emitter, i);
        }
        subTopics.clear();
        emitter << YAML::EndMap;
    }
} // namespace config
